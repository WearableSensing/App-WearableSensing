/*
* This file implements the integration between Wearable Sensing DSI C/C++ API and
* the LSL library.
*
* This program is used for acquiring data from a DSI headset and streaming it
* over the Lab Streaming Layer (LSL) protocol, which allows for real-time
* data acquisition and analysis.

* This program utilizes windows threads to handle the DSI headset processing
* and impedance checking in parallel, allowing for real-time data acquisition.
* DSI headset processing threads continuously call DSI_Headset_Idle to process
* incoming data, while the impedance thread checks for impedance activity and
* prints results.
*
* Please create a GitHub Issue or contact support@wearablesensing.com if you
* encounter any issues or would like to request new features.
*/

#include "DSI.h"
#include "lsl_c.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <windows.h>


/* Helper functions and macros that will be defined down below. */
int               StartUp( int argc, const char * argv[], DSI_Headset *headsetOut, int * helpOut );
int                Finish( DSI_Headset h );
int            GlobalHelp( int argc, const char * argv[] );
lsl_outlet        InitLSL( DSI_Headset h, const char * streamName);
void             OnSample( DSI_Headset h, double packetOffsetTime, void * userData);
void      getRandomString( char *s, const int len);
const char * GetStringOpt( int argc, const char * argv[], const char * keyword1, const char * keyword2 );
int         GetIntegerOpt( int argc, const char * argv[], const char * keyword1, const char * keyword2, int defaultValue );
int        startAnalogReset( DSI_Headset h );  
int          CheckImpedance( DSI_Headset h ); 
void        PrintImpedances( DSI_Headset h, double packetOffsetTime, void * userData );

float *sample;
static volatile int KeepRunning = 1;
static volatile int DSI_Thread_Paused = 0;
void  QuitHandler(int a){ KeepRunning = 0; }

/* Error checking machinery. */
#define REPORT( fmt, x )  fprintf( stderr, #x " = " fmt "\n", ( x ) )
int CheckError( void ){
  if( DSI_Error() ) return fprintf( stderr, "%s\n", DSI_ClearError() );
  else return 0;
}
#define CHECK   if( CheckError() != 0 ) return -1;
#define MAX_COMMAND_LENGTH 256
/* Time delay value used with Sleep() and DSI_Sleep() function calls 
* to prevent busy-waiting and allow other threads to run.
* This value is set to 2 seconds, which is sufficient for most operations
*/
#define BUFFER_SECONDS 2

/* Custom Struct to handle impedance flags. */
typedef struct {
    DSI_Headset h;
    volatile int         printFlag;
    volatile int         startFlag;
    volatile int         stopFlag;
} ThreadParams;

/**
* This function runs in a separate thread to continuously call DSI_Headset_Idle.
*
* @param lpParam - Long Pointer to VOID, holds the needed DSI_Headset handle.
*                  This is cast to DSI_Headset type within the function.
* @return 0 on success, non-zero on error.
*/
DWORD WINAPI DSI_Processing_Thread(LPVOID lpParam) {
    DSI_Headset h = (DSI_Headset)lpParam;
    fprintf(stdout, "DSI processing thread started.\n");

    while (KeepRunning == 1) {
        /* Only call Idle if the main thread hasn't paused us. */
        if (!DSI_Thread_Paused) {
            DSI_Headset_Idle(h, 0.0);
            if (CheckError() != 0) {
                fprintf(stderr, "Error in DSI processing thread. Exiting.\n");
                KeepRunning = 0; /* Signal main thread to exit. */
            }
        }
        /* Sleep for a tiny amount of time to prevent CPU overload. */
        Sleep(BUFFER_SECONDS);
    }

    fprintf(stdout, "DSI processing thread finished.\n");
    return 0;
}

/**
* This function runs in a separate thread to continuously check for impedance activity.
*
* @param lpParam - Long Pointer to VOID, holds the needed DSI_Headset handle and flags.
*                  This is cast to ThreadParams type within the function.
* @return 0 on success
*/
DWORD WINAPI ImpedanceThread(LPVOID lpParam) {
    fprintf(stdout, "DSI impedance thread started.\n");
    ThreadParams *params = (ThreadParams *)lpParam;
    DSI_Headset h = params->h;

    while(KeepRunning == 1){
      if(params->startFlag){
        CheckImpedance( h ); CHECK
        params->startFlag = 0;
      }
      // uncomment the following lines to continuously print impedance check
      // while (params->printFlag) {
      //     DSI_Headset_Receive( h, 0.1, 0 ); CHECK
      // }
      if(params->stopFlag){
        DSI_Headset_StopImpedanceDriver( h ); CHECK
        fprintf(stderr, "\n----------Stopped Impedance Driver-------------\n");
        params->stopFlag = 0;
      }
      
      if (CheckError() != 0) {
          fprintf(stderr, "Error in DSI processing thread. Exiting.\n");
          KeepRunning = 0; /* Signal main thread to exit. */
      }
    }
    
    fprintf(stdout, "DSI impedance thread finished.\n");
    return 0;
}

int main( int argc, const char * argv[] )
{
  /* First load the libDSI dynamic library. */
  const char * dllname = NULL;
  char command[MAX_COMMAND_LENGTH]; /* Buffer to store the user's command */
  char *token;                      /* Pointer to store tokens (parts of the command) */

  HANDLE sThread, iThread;

	/* Load the DSI DLL */
  int load_error = Load_DSI_API( dllname );
  if( load_error < 0 ) return fprintf( stderr, "failed to load dynamic library \"%s\"\n", DSI_DYLIB_NAME( dllname ) );
  if( load_error > 0 ) return fprintf( stderr, "failed to import %d functions from dynamic library \"%s\"\n", load_error, DSI_DYLIB_NAME( dllname ) );
  fprintf( stderr, "DSI API version %s loaded\n", DSI_GetAPIVersion() );
  if( strcmp( DSI_GetAPIVersion(), DSI_API_VERSION ) != 0 ) fprintf( stderr, "WARNING - mismatched versioning: program was compiled with DSI.h version %s but just loaded shared library version %s. You should ensure that you are using matching versions of the API files - contact Wearable Sensing if you are missing a file.\n", DSI_API_VERSION, DSI_GetAPIVersion() );

  /* Implements a Ctrl+C signal handler to quit the program (some terminals actually use Ctrl+Shift+C instead). */
  signal(SIGINT, QuitHandler);

	/* Init the API and headset */
  DSI_Headset h;
  int help, error;
  error = StartUp( argc, argv, &h, &help );
  if( error || help){
    GlobalHelp(argc, argv);
    return error;
  }


  /* Initialize LSL outlet */
  const char * streamName = GetStringOpt(  argc, argv, "lsl-stream-name",   "m" );
  if (!streamName) 
		streamName = "WS-default";
  fprintf(stdout, "Initializing %s outlet\n", streamName);
  lsl_outlet outlet = InitLSL(h, streamName); CHECK; /* Stream outlet */

  /* Set the sample callback (forward every data sample received to LSL) */
  DSI_Headset_SetSampleCallback( h, OnSample, outlet ); CHECK

  /* Start data acquisition */
  fprintf(stdout, "Starting data acquisition\n");
  DSI_Headset_StartDataAcquisition( h ); CHECK

  /* Custom struct for impedance flags */
  ThreadParams zFLag;
  zFLag.h = h; /* Valid DSI_Headset variable */
  zFLag.printFlag = 0; //used to print impedance continuously
  zFLag.startFlag = 0;
  zFLag.stopFlag = 0;

  /* Create the impedance thread */
  iThread = CreateThread(NULL, 0, ImpedanceThread, &zFLag, 0, NULL);
  if (iThread == NULL) {
      fprintf(stderr, "Error creating DSI processing thread.\n");
      return Finish(h);
  }
   /* Create and start the DSI processing thread */
  sThread = CreateThread(NULL, 0, DSI_Processing_Thread, h, 0, NULL);
  if (sThread == NULL) {
      fprintf(stderr, "Error creating DSI processing thread.\n");
      return Finish(h);
  }
  
  fprintf(stderr, "Wait...\n");
  Sleep(BUFFER_SECONDS);
  fprintf(stderr, "Setup Ready\n");
  /* Start streaming */
  fprintf(stdout, "Streaming...\n");
  while( KeepRunning==1 ){
    
    /* 
    * Read a line of input from stdin (the terminal)
    * fgets reads up to MAX_COMMAND_LENGTH-1 characters or until a newline.
    * It includes the newline character if it fits in the buffer. 
    */
    if (fgets(command, MAX_COMMAND_LENGTH, stdin) == NULL) {
        /* Handle potential error or EOF (End Of File) condition. */
        fprintf(stdout, "Error reading input or EOF reached.\n");
        break; /* Exit the loop on error */
    }

    /* 
    * Remove the trailing newline character if it exists
    * fgets includes the newline, which can interfere with string comparisons.
    */
    command[strcspn(command, "\n")] = 0;

    if (command == NULL) {
        /* If no command was entered (just spaces or empty line after stripping newline) */
        continue; /* Go back to the prompt */
    }

    else if (strcmp(command, "checkZOn") == 0) {
        // zFLag.boolFlag = 1;   // uncomment to print impedance continuously
        zFLag.stopFlag = 0;
        zFLag.startFlag = 1;

    }else if (strcmp(command, "checkZOff") == 0) {
        // zFLag.boolFlag = 0;   // uncomment to print impedance continuously
        zFLag.stopFlag = 1;
        zFLag.startFlag = 0;
    }
    else if (strcmp(command, "resetZ") == 0) {
        /* Reset impedance */
        startAnalogReset( h ); CHECK
    }
    Sleep(BUFFER_SECONDS);
  }

  /* Closing the threads */
  if (sThread != NULL) {
      fprintf(stdout, "Waiting for DSI thread to terminate...\n");
      WaitForSingleObject(sThread, INFINITE);
      CloseHandle(sThread);
      fprintf(stdout, "DSI thread has terminated.\n");
  }
  if (iThread != NULL) {
      fprintf(stdout, "Waiting for impedance thread to terminate...\n");
      WaitForSingleObject(iThread, INFINITE);
      CloseHandle(iThread);
      fprintf(stdout, "Impedance thread has terminated.\n");
  }
  

  /* Gracefully exit the program */
  fprintf(stdout, "\n%s will exit now...\n", argv[ 0 ]);
  lsl_destroy_outlet(outlet);
  return Finish( h );
}

/**
 * Reset the analog impedance for a DSI headset.
 *
 * @param h - Valid DSI headset handle
 * @return 0 on success
 */
int startAnalogReset(DSI_Headset h) {
    if (h == NULL) {
        fprintf(stderr, "Error: Invalid headset handle.\n");
        return -1;
    }
    fprintf( stderr, "%s\n", "---------Starting Analog Reset----------------\n" ); CHECK
  
    
    /* Check initial analog reset mode */
    fprintf(stdout, "--> Initial analog reset mode: %d\n", DSI_Headset_GetAnalogResetMode(h));
    

    DSI_Headset_StartAnalogReset(h);
    CHECK;
    
    DSI_Sleep(BUFFER_SECONDS);
    
    fprintf( stderr, "%s\n", "---------Analog Reset Complete----------------\n" ); 
    fflush(stderr);
    return 0;
}

/**
 * Starts checking impedance and format it ready for print
 *
 * @param h - Valid DSI headset handle
 * @return 0 on success, non-zero on error.
 */
int CheckImpedance( DSI_Headset h ){
  fprintf( stderr, "%s\n", "---------Starting Impedance Driver----------------\n" ); CHECK
  DSI_Headset_StartImpedanceDriver( h ); CHECK
  /*
  * The impedance driver injects current at 110Hz and 130Hz, to
  * allow impedances to be measured. It is off by default when
  * you initialize the headset.
  */

  // PrintImpedances( h, 0, "headings" ); CHECK 
  // /* Prints the column headings for our csv output. */

  // DSI_Headset_SetSampleCallback( h, PrintImpedances, NULL ); CHECK
  /*
  * This registers the callback we defined earlier, ensuring that
  * impedances are printed to stdout every time a new sample arrives
  * during `DSI_Headset_Idle()` or `DSI_Headset_Receive()`.
  */
  return 0;
}


/* Handler called on every sample, immediately forwards to LSL */
void OnSample( DSI_Headset h, double packetOffsetTime, void * outlet)
{
  unsigned int channelIndex;
  unsigned int numberOfChannels = DSI_Headset_GetNumberOfChannels( h );
  if (sample == NULL) 
		sample = (float *)malloc( numberOfChannels * sizeof(float));
  for(channelIndex=0; channelIndex < numberOfChannels; channelIndex++){
    sample[channelIndex] = (float) DSI_Channel_GetSignal( DSI_Headset_GetChannelByIndex( h, channelIndex ) );
  }
  lsl_push_sample_f(outlet, sample);
}

int Message( const char * msg, int debugLevel ){
  return fprintf( stderr, "DSI Message (level %d): %s\n", debugLevel, msg );
}

/**
 * Initializes and connects to the DSI headset, prepares it for
 * data acquisition.
 *
 * @param argc - Command-line argument count
 * @param argv - Command-line argument vector
 * @param headsetOut - Output pointer to store initialized headset handle.
 * @param helpOut - Output flag indicating if help was requested.
 *
 * @return 0 on success, non-zero on error.
 */
int StartUp( int argc, const char * argv[], DSI_Headset * headsetOut, int * helpOut )
{
  DSI_Headset h;

  /* Read out any configuration options. */
  int          help       = GetStringOpt(  argc, argv, "help",      "h" ) != NULL;
  const char * serialPort = GetStringOpt(  argc, argv, "port",      "p" );
  const char * montage    = GetStringOpt(  argc, argv, "montage",   "m" );
  const char * reference  = GetStringOpt(  argc, argv, "reference", "r" );
  int          verbosity  = GetIntegerOpt( argc, argv, "verbosity", "v", 2 );
  if( headsetOut ) *headsetOut = NULL;
  if( helpOut ) *helpOut = help;
  if( help ) return 0;

  /* Passing NULL defers setup of the serial port connection until later... */
  h = DSI_Headset_New( NULL ); CHECK

  /*
  * ...which allows us to configure the way we handle any debugging messages
  * that occur during connection (see our definition of the `DSI_MessageCallback`
  * function `Message()` above).
  */
  DSI_Headset_SetMessageCallback( h, Message ); CHECK
  DSI_Headset_SetVerbosity( h, verbosity ); CHECK

  /*
  * Now we establish the serial port connection and initialize the headset.
  * In this demo program, the string supplied in the --port command-line
  * option is used as the serial port address (if this string is empty, the
  * API will automatically look for an environment variable called
  * DSISerialPort).
  * */
  DSI_Headset_Connect( h, serialPort ); CHECK

  /*
  * Sets up the montage according to strings supplied in the --montage and
  * --reference command-line options, if any.
  */
  DSI_Headset_ChooseChannels( h, montage, reference, 1 ); CHECK

  /* Prints an overview of what is known about the headset. */
  fprintf( stderr, "%s\n", DSI_Headset_GetInfoString( h ) ); CHECK


  if( headsetOut ) *headsetOut = h;
  if( helpOut ) *helpOut = help;
  return 0;
}
                                                                              
/* Close connection to the hardware */
int Finish( DSI_Headset h )
{
  /* This stops our application from responding to received samples. */
  DSI_Headset_SetSampleCallback( h, NULL, NULL ); CHECK

  /* This send a command to the headset to tell it to stop sending samples. */
  DSI_Headset_StopDataAcquisition( h ); CHECK

  /*
  * This allows more than enough time to receive any samples that were
  * sent before the stop command is carried out, along with the alarm
  * signal that the headset sends out when it stops.
  */
  DSI_Headset_Idle( h, 1.0 ); CHECK

  /*
  * This is the only really necessary step. Disconnects from the serial
  * port, frees memory, etc.
  */
  DSI_Headset_Delete( h ); CHECK

  return 0;
}

/**
 * Generates a random alphanumeric string used for the LSL source ID.
 *
 * @param s - Output buffer to store generated string
 * @param len - Length of random generated string
 * @return void
 */

void getRandomString(char *s, const int len)
{
  int i = 0;
  static const char alphanum[] =     "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  for (i=0; i < len; ++i){ s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];}
  s[len] = 0;
}

lsl_outlet InitLSL(DSI_Headset h, const char * streamName)
{
  unsigned int channelIndex;
  unsigned int numberOfChannels = DSI_Headset_GetNumberOfChannels( h );
  int samplingRate = DSI_Headset_GetSamplingRate( h );

	/* Out stream declaration object */
  lsl_streaminfo info;
	/* Some xml element pointers */
  lsl_xml_ptr desc, chn, chns, ref; 
  int imax = 16;
  char source_id[16];
  char *long_label;
  char *short_label;
  char *reference;
	
	/* Note: an even better choice here may be the serial number of the device. */
  getRandomString(source_id, imax);

  /* Declare a new streaminfo (name: WearableSensing, content type: EEG, number of channels, srate, float values, source id. */
  info = lsl_create_streaminfo((char*)streamName,"EEG",numberOfChannels,samplingRate,cft_float32,source_id);

  /* Add some meta-data fields to it (for more standard fields, see https://github.com/sccn/xdf/wiki/Meta-Data). */
  desc = lsl_get_desc(info);
  lsl_append_child_value(desc,"manufacturer","WearableSensing");
	
	/* Describe channel info */
  chns = lsl_append_child(desc,"channels");
  for( channelIndex=0; channelIndex < numberOfChannels ; channelIndex++)
  {
    chn = lsl_append_child(chns,"channel");

    long_label = (char*) DSI_Channel_GetString( DSI_Headset_GetChannelByIndex( h, channelIndex ) );
		/* Cut off "negative" part of channel name (e.g., the ref chn) */
    short_label = strtok(long_label, "-");
    if(short_label == NULL)
      short_label = long_label;
		/* Cmit channel info */
    lsl_append_child_value(chn,"label", short_label);
    lsl_append_child_value(chn,"unit","microvolts");
    lsl_append_child_value(chn,"type","EEG");
  }
	
	/* Describe reference used */
  reference = (char*)DSI_Headset_GetReferenceString(h);
  ref = lsl_append_child(desc,"reference");
  lsl_append_child_value(ref,"label", reference);
  fprintf(stdout, "REF: %s\n", reference);

  /* Make a new outlet (chunking: default, buffering: 360 seconds). */
  return lsl_create_outlet(info,0,360);
}


int GlobalHelp( int argc, const char * argv[] )
{
  fprintf( stderr,
            "Usage: %s [ --OPTIONS... ]\n\n"
            "With the exception of --help,\n"
            "the options should be given in --NAME=VALUE format.\n"
            "\n"
            "  --help\n"
            "       Displays this help text.\n"
            "\n"
            "  --port\n"
            "       Specifies the serial port address (e.g. --port=COM4 on Windows,\n"
            "       --port=/dev/cu.DSI24-023-BluetoothSeri on OSX, or --port=/dev/rfcomm0 on Linux) on which to connect.\n"
            "       Note: if you omit this option, or use an empty string or the string\n"
            "       \"default\", then the API will look for an environment variable called\n"
            "       DSISerialPort and use the content of that, if available.\n"
            "\n"
            "  --montage\n"
            "       A list of channel specifications, comma-separated without spaces,\n"
            "       (can also be space-delimited, but then you would need to enclose the\n"
            "       option in quotes on the command-line).\n"
            "\n"
            "  --reference\n"
            "       The name of sensor (or linear combination of sensors, without spaces)\n"
            "       to be used as reference. Defaults to a \"traditional\" averaged-ears or\n"
            "       averaged-mastoids reference if available, or the factory reference\n"
            "       (typically Pz) if these sensors are not available.\n"
            "\n"
            "  --verbosity\n"
            "       The higher the number, the more messages the headset will send to the\n"
            "       registered `DSI_MessageCallback` function, and hence to the console\n"
            "       (and the more low-level they will tend to be)\n"
            "\n"
            "  --lsl-stream-name\n"
            "       The name of the LSL outlet that will be created to stream the samples\n"
            "       received from the device. If omitted, the stream will be given the name WS-default.\n"
            "\n"
        , argv[ 0 ] );
        return 0;
    }


/*
* These two functions are carried over from the Wearable Sensing example code 
* and are Copyright (c) 2014-2016 Wearable Sensing LLC.
*
* Helper function for figuring out command-line input flags like --port=COM4
* or /port:COM4 (either syntax will work).  Returns NULL if the specified
* option is absent. Returns a pointer to the argument value if the option
* is present (the pointer will point to '\0' if the argument value is empty
* or not supplied as part of the option string). 
*/
const char * GetStringOpt( int argc, const char * argv[], const char * keyword1, const char * keyword2 )
{
    int i, j;
    const char * result = NULL;
    const char * keyword;
    for( i = 1; i < argc; i++ )
    {
        int isopt = 0;
        const char * arg = argv[ i ];
        if( !arg ) continue;
        for( j = 0; arg[ j ]; j++ ) isopt |= arg[ j ] == '-' || arg[ j ] == '=' || arg[ j ] == '/' || arg[ j ] == ':';
        if( *arg == '-' || *arg == '/' ) ++arg;
        if( *arg == '-' || *arg == '/' ) ++arg;
        for( j = 0, keyword = keyword1; j < 2; j++, keyword = keyword2  )
        {
            if( keyword && strncmp( arg, keyword, strlen( keyword ) ) == 0 )
            {
                const char * potential = arg + strlen( keyword );
                if( *potential == '=' || *potential == ':' ) result = potential + 1;
                else if( *potential == '\0' || ( *keyword == '\0' && !isopt ) ) result = potential;
            }
        }
    }
    return result;
}

int GetIntegerOpt( int argc, const char * argv[], const char * keyword1, const char * keyword2, int defaultValue )
{
    char * end;
    int result;
    const char * stringValue = GetStringOpt( argc, argv, keyword1, keyword2 );
    if( !stringValue || !*stringValue ) return defaultValue;
    result = ( int ) strtol( stringValue, &end, 10 );
    if( !end || !*end ) return result;
    fprintf( stderr, "WARNING: could not interpret \"%s\" as a valid integer value for the \"%s\" option - reverting to default value %s=%g\n", stringValue, keyword1, keyword1, ( double )defaultValue );
    return defaultValue;
}



void PrintImpedances( DSI_Headset h, double packetOffsetTime, void * userData )
{
    unsigned int sourceIndex;
    unsigned int numberOfSources = DSI_Headset_GetNumberOfSources( h );

    /*
    * This function uses `userData` as nothing more than a crude boolean
    * flag: when it is non-zero, we'll print headings; when it is zero,
    * we'll print impedance values.
    */

    if( userData ) fprintf( stdout, "%9s",    "Time" );
    else           fprintf( stdout, "% 9.4f", packetOffsetTime );

    for( sourceIndex = 0; sourceIndex < numberOfSources; sourceIndex++ )
    {
        DSI_Source s = DSI_Headset_GetSourceByIndex( h, sourceIndex );

        if( DSI_Source_IsReferentialEEG( s ) && ! DSI_Source_IsFactoryReference( s ) )
        {
            if( userData ) fprintf( stdout, ",%9s",    DSI_Source_GetName( s ) );
            else           fprintf( stdout, ",% 9.4f", DSI_Source_GetImpedanceEEG( s ) );
        }
    }

    /*
    * The common-mode follower (CMF) sensor, at the factory reference position,
    * is a special case:
    */

    if( userData ) fprintf( stdout, ",   CMF=%s\n", DSI_Headset_GetFactoryReferenceString( h ) );
    else           fprintf( stdout, ",% 9.4f\n",    DSI_Headset_GetImpedanceCMF( h ) );
}
