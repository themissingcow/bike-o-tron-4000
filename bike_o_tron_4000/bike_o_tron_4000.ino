

// Pin mappings

int redLED = 10;
int greenLED = 11;

int tachPin = 2;
int modePin = 4;

// Run Modes

#define kRUN 0
#define kMERGE 1
#define kLOG 2

// settings

int updateInterval = 10;   // Runs the main loop every n milliseconds
int sampleInterval = 1000;  // Records progress every n milliseconds
int tachRange = 40; // +/- outside the expected tach


// globals

byte runMode = kRUN;
bool journeyStarted = false;
unsigned long journeyStart = 0;
byte journeyIndex = 0;

// counters

volatile unsigned long totalTach = 0;
unsigned long expectedTach = 0;

// Storagge

unsigned long lastSampleTach = 0; // The last stored totalTachs
unsigned long loopTime = 0;
unsigned long lastSample = 0;
bool modePressed = false;


void setup()
{
  Serial.begin( 9600 );
  
  pinMode( redLED, OUTPUT );
  pinMode( greenLED, OUTPUT ); 
  
  pinMode( tachPin, INPUT );
  attachInterrupt(0, tach_interrupt, RISING);

  pinMode( modePin, INPUT );
  attachInterrupt(1, mode_interrupt, RISING);
}


void tach_interrupt()
{
  // We'll need to look at debouncing when we
  // have the real wheel hardware
   ++totalTach;
}

void mode_interrupt()
{
   modePressed = true;
}


void loop()
{
  if( journeyStarted ) 
  { 
     loop_journey();
  }
  else
  {
     loop_setup(); 
  }
}


unsigned int buttonHighCount = 0;
unsigned long lastSetupLoop = 0;

void loop_setup()
{
  // In this mode, holding down the button cycles between
  // run, record and merge. Pressing mode selects a journey.
  
  // 10hz loop
  if( ( millis() - lastSetupLoop ) < 100 ) { return; }
  
  if( digitalRead(modePin) )
  {
    ++buttonHighCount;
  }
  else
  {
    if( buttonHighCount > 10 )
    {
       ++runMode;
       if( runMode > kLOG )
       {
          runMode = kRUN; 
       }      
    }
    else if ( buttonHighCount > 2 )
    {
       ++journeyIndex;
       if( journeyIndex > 1 )
       {
          journeyIndex = 0; 
       }
    }
    buttonHighCount = 0;
  }
  
  reportModeState();
  
  lastSetupLoop = millis();
}


void loop_journey() 
{
  loopTime = millis() - journeyStart;
  debug(); 
  
  // The Logic
  //
  // At each sample interval, we compare expectedTachs to totalTacks
  // and map the delta to the color range of the output LED
  updateExpected();
  
  reportTachState( expectedTach - totalTach );
  
  // Use delay for simplicity for now
  delay(updateInterval);
}



// Calculates the expected number of tacks at this interval
void updateExpected()
{
   int sampleIndex = loopTime / sampleInterval;
  
   Serial.print( sampleIndex );

   // tmp - fixed velocity
   if( (loopTime - lastSample) > sampleInterval )
   {
     expectedTach += 4;
     lastSample = loopTime;
   }
}


void recordProgress()
{
   int sampleIndex = loopTime / sampleInterval;
   byte sampleDelta = totalTach - lastSampleTach;
   lastSampleTach = totalTach;
}



int flashCount = 0;
byte ledState = 0;
byte modeReportLoopCount = 0;
unsigned long lastModeReport = 0;

void reportModeState()
{
  if( ( millis() - lastModeReport ) < 100 ) { return; }
  lastModeReport = millis();
  
  ++modeReportLoopCount;
  if( modeReportLoopCount < 6 ) { return; }
  
  if( runMode > kRUN )
  {
     analogWrite( redLED, ( runMode == kMERGE ) ? ledState/2 : ledState );
  }
  else
  {
    analogWrite( redLED, 0 );
  }
  
  if( runMode < kLOG )
  {
    analogWrite( greenLED, ledState );
  }
  else
  {
     analogWrite( greenLED, 0 );
  }
  
  if( ledState == 0 ) { ledState = 255; } else { ledState = 0; }
  
  if( ledState == 255 )
  {
     ++flashCount; 
  }
  
  if( flashCount > journeyIndex )
  {
     modeReportLoopCount = 0;
     flashCount = 0; 
  }
}


void reportTachState( int delta )
{
    int redState = map( delta, -tachRange, 5, 0, 200 );
    int greenState = map( delta, -5, tachRange, 255, 0 );
    
    analogWrite( 
      redLED, 
      constrain( redState, 0, 255 )
    );
    
    analogWrite( 
      greenLED, 
      constrain( greenState, 0, 255 )
    );
  
}


void debug()
{
  Serial.print( " loopTime: " );
  Serial.print( loopTime );
  Serial.print( " expectedTach: " );
  Serial.print( expectedTach );
  Serial.print( " tach: " );
  Serial.print( totalTach );
  Serial.print( "\n" );

}


