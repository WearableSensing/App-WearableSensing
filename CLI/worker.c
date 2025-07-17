#include "DSI.h"
#include <stdio.h>
#include <signal.h>
#include <string.h>  // For strncmp()
#include <stdlib.h>  // For strtol()

// Include necessary helper function prototypes from your original file
// You may need to move these into a shared header file (.h)
const char * GetStringOpt( int argc, const char * argv[], const char * keyword1, const char * keyword2 );
int GetIntegerOpt( int argc, const char * argv[], const char * keyword1, const char * keyword2, int defaultValue );
void PrintImpedances( DSI_Headset h, double packetOffsetTime, void * userData );
const char * GetStringOpt( int argc, const char * argv[], const char * keyword1, const char * keyword2 );
int Message( const char * msg, int debugLevel );

// Global flag to stop the loop
static volatile int KeepRunning = 1;
void QuitHandler(int a) { KeepRunning = 0; }

int main(int argc, const char *argv[]) {
    // Handle Ctrl+C to exit gracefully
    signal(SIGINT, QuitHandler);

    // --- Connect to Headset using arguments from controller ---
    const char *serialPort = GetStringOpt(argc, argv, "port", "p");
    fprintf(serialPort);
    DSI_Headset h = DSI_Headset_New(NULL);
    DSI_Headset_SetMessageCallback(h, Message);
    DSI_Headset_Connect(h, serialPort);
    if (DSI_Error()) {
        fprintf(stderr, "Worker: Failed to connect to headset on port %s.\n", serialPort);
        DSI_Headset_Delete(h);
        return 1;
    }
    printf("Worker: Connected to headset %s\n", DSI_Headset_GetInfoString(h));

    // --- Start Impedance Check ---
    DSI_Headset_StartImpedanceDriver(h);
    DSI_Headset_StartDataAcquisition(h);
    DSI_Headset_SetSampleCallback(h, PrintImpedances, NULL);
    
    // Print table headings
    PrintImpedances(h, 0, "headings");

    // Loop forever, printing impedances
    printf("Worker: Printing impedances. Press Ctrl+C in this window to stop.\n");
    while (KeepRunning) {
        DSI_Headset_Idle(h, 1.0); // Wait for data
    }

    // --- Cleanup ---
    printf("Worker: Exiting.\n");
    DSI_Headset_StopDataAcquisition(h);
    DSI_Headset_Delete(h);
    return 0;
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


int Message( const char * msg, int debugLevel ){
  return fprintf( stderr, "DSI Message (level %d): %s\n", debugLevel, msg );
}

