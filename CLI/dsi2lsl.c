/*
This file implements the integration between Wearable Sensing DSI C/C++ API and
the LSL library.

Copyright (C) 2014-2020 Syntrogi Inc dba Intheon.
*/


#include "DSI.h"
#include "lsl_c.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

// Helper functions and macros that will be defined down below
int               StartUp( int argc, const char * argv[], DSI_Headset *headsetOut, int * helpOut );
int                Finish( DSI_Headset h );
int            GlobalHelp( int argc, const char * argv[] );
lsl_outlet        InitLSL( DSI_Headset h, const char * streamName);
void             OnSample( DSI_Headset h, double packetOffsetTime, void * userData);
void      getRandomString( char *s, const int len);
const char * GetStringOpt( int argc, const char * argv[], const char * keyword1, const char * keyword2 );
int         GetIntegerOpt( int argc, const char * argv[], const char * keyword1, const char * keyword2, int defaultValue );
int        startAnalogReset( DSI_Headset h );  
int         CheckImpedance( DSI_Headset h ); 

float *sample;
static volatile int KeepRunning = 1;
void  QuitHandler(int a){ KeepRunning = 0;}

// error checking machinery
#define REPORT( fmt, x )  fprintf( stderr, #x " = " fmt "\n", ( x ) )
int CheckError( void ){
  if( DSI_Error() ) return fprintf( stderr, "%s\n", DSI_ClearError() );
  else return 0;
}
#define CHECK   if( CheckError() != 0 ) return -1;

int main( int argc, const char * argv[] )
{
  // First load the libDSI dynamic library
  const char * dllname = NULL;

	// load the DSI DLL
  int load_error = Load_DSI_API( dllname );
  if( load_error < 0 ) return fprintf( stderr, "failed to load dynamic library \"%s\"\n", DSI_DYLIB_NAME( dllname ) );
  if( load_error > 0 ) return fprintf( stderr, "failed to import %d functions from dynamic library \"%s\"\n", load_error, DSI_DYLIB_NAME( dllname ) );
  fprintf( stderr, "DSI API version %s loaded\n", DSI_GetAPIVersion() );
  if( strcmp( DSI_GetAPIVersion(), DSI_API_VERSION ) != 0 ) fprintf( stderr, "WARNING - mismatched versioning: program was compiled with DSI.h version %s but just loaded shared library version %s. You should ensure that you are using matching versions of the API files - contact Wearable Sensing if you are missing a file.\n", DSI_API_VERSION, DSI_GetAPIVersion() );

  // Implements a Ctrl+C signal handler to quit the program (some terminals actually use Ctrl+Shift+C instead)
  signal(SIGINT, QuitHandler);

	// init the API and headset
  DSI_Headset h;
  int help, error;
  error = StartUp( argc, argv, &h, &help );
  if( error || help){
    GlobalHelp(argc, argv);
    return error;
  }

  // Initialize LSL outlet
  const char * streamName = GetStringOpt(  argc, argv, "lsl-stream-name",   "m" );
  if (!streamName) 
		streamName = "WS-default";
  printf("Initializing %s outlet\n", streamName);
  lsl_outlet outlet = InitLSL(h, streamName); CHECK; /* stream outlet */

  // Set the sample callback (forward every data sample received to LSL)
  DSI_Headset_SetSampleCallback( h, OnSample, outlet ); CHECK

  // Start data acquisition
  printf("Starting data acquisition\n");
  DSI_Headset_StartDataAcquisition( h ); CHECK

  // Start streaming
  printf("Streaming...\n");
  while( KeepRunning==1 ){
    DSI_Headset_Idle( h, 0.0 ); CHECK
  }

  // Gracefully exit the program
  printf("\n%s will exit now...\n", argv[ 0 ]);
  lsl_destroy_outlet(outlet);
  return Finish( h );
}




void PrintImpedances( DSI_Headset h, double packetOffsetTime, void * userData )
{
    unsigned int sourceIndex;
    unsigned int numberOfSources = DSI_Headset_GetNumberOfSources( h );

    // This function uses `userData` as nothing more than a crude boolean
    // flag: when it is non-zero, we'll print headings; when it is zero,
    // we'll print impedance values.

    if( userData ) printf( "%9s",    "Time" );
    else           printf( "% 9.4f", packetOffsetTime );

    for( sourceIndex = 0; sourceIndex < numberOfSources; sourceIndex++ )
    {
        DSI_Source s = DSI_Headset_GetSourceByIndex( h, sourceIndex );

        if( DSI_Source_IsReferentialEEG( s ) && ! DSI_Source_IsFactoryReference( s ) )
        {
            if( userData ) printf( ",%9s",    DSI_Source_GetName( s ) );
            else           printf( ",% 9.4f", DSI_Source_GetImpedanceEEG( s ) );
        }
    }

    // The common-mode follower (CMF) sensor, at the factory reference position,
    // is a special case:

    if( userData ) fprintf( stdout, ",   CMF=%s\n", DSI_Headset_GetFactoryReferenceString( h ) );
    else           fprintf( stdout, ",% 9.4f\n",    DSI_Headset_GetImpedanceCMF( h ) );
}

int startAnalogReset(DSI_Headset h) {
    if (h == NULL) {
        printf("Error: Invalid headset handle.\n");
        return -1;
    }

    printf("=== ANALOG RESET PROCEDURE ===\n");
    
    // Check initial analog reset mode
    printf("--> Initial analog reset mode: %d\n", DSI_Headset_GetAnalogResetMode(h));
    
    printf("--> Sending analog reset command (StartAnalogReset)...\n");
    DSI_Headset_StartAnalogReset(h);
    CHECK;
    
    // Give it a moment to take effect
    printf("--> Waiting 3 seconds for reset to complete...\n");
    DSI_Sleep(3.0);
    
    printf("--> Analog reset complete!\n");

    // Check impedances after reset
    printf("\n=== CHECKING IMPEDANCES AFTER RESET ===\n");
    CheckImpedance(h);

    printf("=== END ANALOG RESET PROCEDURE ===\n");
    return 0;
}

int CheckImpedance( DSI_Headset h){
  double durationSec = 5;
  // ------------------------------------------------------
  //stops the exisiting data acquisition
  DSI_Headset_StopDataAcquisition( h );

  // starts impedance driver
  fprintf( stderr, "%s\n", "---------Starting Impedance Driver----------------\n" ); CHECK
  DSI_Headset_StartImpedanceDriver( h ); CHECK
  // The impedance driver injects current at 110Hz and 130Hz, to
  // allow impedances to be measured. It is off by default when
  // you initialize the headset.

  fprintf( stderr, "%s\n", "---------Starting Data Acquisition----------------\n" ); CHECK
  DSI_Headset_StartDataAcquisition( h ); CHECK
  // This starts the sample-by-sample flow of data from the headset.

  DSI_Headset_Idle( h, 1.0 ); CHECK
  // Let's not print impedances until they have settled for a second.

  PrintImpedances( h, 0, "headings" ); CHECK //this functions doesnt make sense ---think this just prints it, so we will want to print it in the terminal of lsl gui instead---------------------
  // According to the way we set up `PrintImpedances`, above, calling
  // it this way prints the column headings for our csv output.

  DSI_Headset_SetSampleCallback( h, PrintImpedances, NULL ); CHECK
  // This registers the callback we defined earlier, ensuring that
  // impedances are printed to stdout every time a new sample arrives
  // during `DSI_Headset_Idle()` or `DSI_Headset_Receive()`.

  while(1){
    DSI_Headset_Receive( h, 1, 0 ); CHECK //(headset, how long, extra time after turned off)
      // This is really a shortcut for quick-and-dirty development: it
      // turns on data acquisition mode if it is not already on, then
      // processes events for the specified number of seconds---the same
      // as `DSI_Headset_Idle( h, durationSec )`---or until data
      // acquisition is otherwise stopped. It then ensures data
      // acquisition is turned off and processes events for a further 1
      // second. (A negative duration in either argument would mean
      // "process forever".) Typically, you will want to use
      // `DSI_Headset_Idle( h, 0)` inside your own main loop instead of
      // a single call to `DSI_Headset_Receive()`.
      // ------------------------------------------------------
      // DSI_Headset_StopImpedanceDriver( h ); CHECK
  }

  return 0;
}


// handler called on every sample, immediately forwards to LSL
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

// connect to headset
int StartUp( int argc, const char * argv[], DSI_Headset * headsetOut, int * helpOut )
{
  DSI_Headset h;

  // read out any configuration options
  int          help       = GetStringOpt(  argc, argv, "help",      "h" ) != NULL;
  const char * serialPort = GetStringOpt(  argc, argv, "port",      "p" );
  const char * montage    = GetStringOpt(  argc, argv, "montage",   "m" );
  const char * reference  = GetStringOpt(  argc, argv, "reference", "r" );
  int          verbosity  = GetIntegerOpt( argc, argv, "verbosity", "v", 2 );
  if( headsetOut ) *headsetOut = NULL;
  if( helpOut ) *helpOut = help;
  if( help ) return 0;

  // Passing NULL defers setup of the serial port connection until later...
  h = DSI_Headset_New( NULL ); CHECK

  // ...which allows us to configure the way we handle any debugging messages
  // that occur during connection (see our definition of the `DSI_MessageCallback`
  // function `Message()` above).
  DSI_Headset_SetMessageCallback( h, Message ); CHECK
  DSI_Headset_SetVerbosity( h, verbosity ); CHECK

  // Now we establish the serial port connection and initialize the headset.
  // In this demo program, the string supplied in the --port command-line
  // option is used as the serial port address (if this string is empty, the
  // API will automatically look for an environment variable called
  // DSISerialPort).
  DSI_Headset_Connect( h, serialPort ); CHECK

  // Sets up the montage according to strings supplied in the --montage and
  // --reference command-line options, if any.
  DSI_Headset_ChooseChannels( h, montage, reference, 1 ); CHECK

  // prints an overview of what is known about the headset
  fprintf( stderr, "%s\n", DSI_Headset_GetInfoString( h ) ); CHECK

    // -------------------------------------------check impedance on startup--------------------]
  fprintf( stderr, "%s\n", "-------------checking impedances now-------------\n" ); CHECK
  CheckImpedance( h ); CHECK

  if( headsetOut ) *headsetOut = h;
  if( helpOut ) *helpOut = help;
  return 0;
}
                                                                              
// close connection to the hardware
int Finish( DSI_Headset h )
{
  // This stops our application from responding to received samples.
  DSI_Headset_SetSampleCallback( h, NULL, NULL ); CHECK

  // This send a command to the headset to tell it to stop sending samples.
  DSI_Headset_StopDataAcquisition( h ); CHECK

  // This allows more than enough time to receive any samples that were
  // sent before the stop command is carried out, along with the alarm
  // signal that the headset sends out when it stops
  DSI_Headset_Idle( h, 1.0 ); CHECK

  // This is the only really necessary step. Disconnects from the serial
  // port, frees memory, etc.
  DSI_Headset_Delete( h ); CHECK

  return 0;
}


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

	// out stream declaration object 
  lsl_streaminfo info;
	// some xml element pointers
  lsl_xml_ptr desc, chn, chns, ref; 
  int imax = 16;
  char source_id[16];
  char *long_label;
  char *short_label;
  char *reference;
	
	// Note: an even better choice here may be the serial number of the device
  getRandomString(source_id, imax);

  // Declare a new streaminfo (name: WearableSensing, content type: EEG, number of channels, srate, float values, source id
  info = lsl_create_streaminfo((char*)streamName,"EEG",numberOfChannels,samplingRate,cft_float32,source_id);

  // Add some meta-data fields to it (for more standard fields, see https://github.com/sccn/xdf/wiki/Meta-Data) 
  desc = lsl_get_desc(info);
  lsl_append_child_value(desc,"manufacturer","WearableSensing");
	
	// Describe channel info
  chns = lsl_append_child(desc,"channels");
  for( channelIndex=0; channelIndex < numberOfChannels ; channelIndex++)
  {
    chn = lsl_append_child(chns,"channel");

    long_label = (char*) DSI_Channel_GetString( DSI_Headset_GetChannelByIndex( h, channelIndex ) );
		// Cut off "negative" part of channel name (e.g., the ref chn)
    short_label = strtok(long_label, "-");
    if(short_label == NULL)
      short_label = long_label;
		// Cmit channel info 
    lsl_append_child_value(chn,"label", short_label);
    lsl_append_child_value(chn,"unit","microvolts");
    lsl_append_child_value(chn,"type","EEG");
  }
	
	// Describe reference used 
  reference = (char*)DSI_Headset_GetReferenceString(h);
  ref = lsl_append_child(desc,"reference");
  lsl_append_child_value(ref,"label", reference);
  printf("REF: %s\n", reference);

  // Make a new outlet (chunking: default, buffering: 360 seconds)
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
            "  --analog-reset\n"
            "       Performs analog reset procedure and exits (does not start streaming).\n"
            "       This command temporarily grounds the amplifier inputs to clear DC offsets,\n"
            "       which can build up due to sensor movement during setup.\n"
            "       Useful for quickly stabilizing signals after adjusting electrodes.\n"
            "       It is recommended to wait at least 1 second after issuing the reset\n"
            "       before taking new measurements or repeating the command.\n"
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


// These two functions are carried over from the Wearable Sensing example code 
// and are Copyright (c) 2014-2016 Wearable Sensing LLC
//
// Helper function for figuring out command-line input flags like --port=COM4
// or /port:COM4 (either syntax will work).  Returns NULL if the specified
// option is absent. Returns a pointer to the argument value if the option
// is present (the pointer will point to '\0' if the argument value is empty
// or not supplied as part of the option string). 

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
