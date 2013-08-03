#include <SPI.h>
#include <SPIFlash.h>

// Pin mappings

int redLED = 10;
int greenLED = 9;

int tachPin = 3;
int modePin = 7;

// Run Modes

#define kRUN 0
#define kMERGE 1
#define kLOG 2

// settings

int updateInterval = 10;   // Runs the main loop every n milliseconds
int logInterval = 5000;  // Records progress every n milliseconds
int tachRange = 600; // +/- outside the expected tach
int tachOffset = tachRange - 50;

// globals

byte runMode = kRUN;
bool journeyStarted = false;
unsigned long journeyStart = 0;
byte journeyIndex = 0;

// counters

volatile bool tachHigh = false;
unsigned int tachHighCount = 0;

unsigned long totalTach = 0;
unsigned long expectedTach = 0;
unsigned long expectedInterpolatedTach = 0;
unsigned long lastSampleTach = 0; // The last stored totalTachs
unsigned long sampleIndex = 0;

// Storagge

unsigned long loopTime = 0;
unsigned long journeyStartTime = 0;
unsigned long lastRunLoopTime = 0;
unsigned long lastLogLoopTime = 0;
unsigned long lastTachLoopTime = 0;

// interpolation variables
unsigned long lastSample = 0;

// Memory Addresses
SPIFlash flash(8, 0xEF30);

unsigned long currentStartAddr = 0x00;
unsigned long currentEndAddr = 0x00;

unsigned int journeyBlockCount = 24;

// First block (4096 bytes) reserved

// Memory Indexes of journey starts, we use 24x4k blocks per journey
unsigned long k_a_startAddr = 0x00000 + 4096;
unsigned long k_b_startAddr = k_a_startAddr + journeyBlockCount * 4096;
unsigned long k_c_startAddr = k_b_startAddr + journeyBlockCount * 4096;
unsigned long k_d_startAddr = k_c_startAddr + journeyBlockCount * 4096;
unsigned long k_x_startAddr = k_d_startAddr + journeyBlockCount * 4096;


void setup()
{  
  pinMode( redLED, OUTPUT );
  pinMode( greenLED, OUTPUT ); 
  
  pinMode( tachPin, INPUT );
  attachInterrupt(1, tach_interrupt, RISING);

  pinMode( modePin, INPUT );
  
  flash.initialize();
}


void tach_interrupt()
{
   tachHigh = true;
}


void loop()
{
  loopTime = millis();
  
   // Software debouncing of the tach input
   // On a 100hz loop
  if( (loopTime - lastTachLoopTime) > 10 )
  {
    if( tachHigh )
    {
       ++tachHighCount;
       
      //  Make sure we've been high twice == 50hz max tach
      if( tachHighCount > 2 )
      {
        ++totalTach;
        tachHighCount = 0;
        tachHigh = false;
      }
    }
    lastTachLoopTime = loopTime;
  }
  
  if( journeyStarted ) 
  { 
     if( runMode > kRUN )
     {
        loop_log(); 
     }
     else
     {
        loop_journey();
     }
  }
  else
  {
     loop_setup();
   
  }
}


unsigned int buttonHighCount = 0;
void loop_setup()
{
  // In this mode, holding down the button cycles between
  // run, record and merge. Pressing mode selects a journey.
  
  // 10Hz setup loop
  if( ( loopTime - lastRunLoopTime ) < 100 ) { return; }
  
  if( digitalRead(modePin) )
  {
    ++buttonHighCount;
    totalTach = 0;
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
       totalTach = 0;
    }
    else if ( buttonHighCount > 2 )
    {
       ++journeyIndex;
       if( journeyIndex > 3 )
       {
          journeyIndex = 0; 
       }
       totalTach = 0;

    }
    buttonHighCount = 0;
  }
  
  reportModeState();
  
  // After 4 revolutions, start the journey
  if( totalTach > 4 )
  {
     // Store the current start address of the journey
     if      ( journeyIndex == 0 ) { currentStartAddr = k_a_startAddr; }
     else if ( journeyIndex == 1 ) { currentStartAddr = k_b_startAddr; }
     else if ( journeyIndex == 2 ) { currentStartAddr = k_c_startAddr; }
     else                          { currentStartAddr = k_d_startAddr; }
     
     if( runMode == kLOG )
     {
        eraseJourney( currentStartAddr );
     }
     else if( runMode == kMERGE )
     {
        // erase our placeholder memory
        eraseJourney( k_x_startAddr );
     }
     
     // calculate journey end addr (long) start at 0
     currentEndAddr = currentStartAddr + (4096 * journeyBlockCount);

     reset_counters();
     journeyStarted = true;
  }
  
  lastRunLoopTime = loopTime;
}


void loop_journey() 
{
  if( ( loopTime - lastRunLoopTime ) < updateInterval ) { return; }
    
  updateExpected();
  reportTachState( expectedInterpolatedTach - (totalTach + tachOffset) );
  
  lastRunLoopTime = loopTime;
}


bool ledOn = false;
void loop_log()
{
  if( tachHigh )
  {
    analogWrite(redLED, 255);
    if( runMode == kMERGE )
    {
      analogWrite(greenLED, 255);
    }
    ledOn = true;
  }
  
  if(ledOn && (( loopTime - lastLogLoopTime ) > 100 ))
  {
      analogWrite(redLED, 0); 
      analogWrite(greenLED, 0);
      ledOn = false;
  }
  

  if( (loopTime - lastLogLoopTime) > logInterval )
  {
    lastLogLoopTime = loopTime;
    
    recordProgress(sampleIndex);
     ++sampleIndex;
     
    // Handle user interaction

    if( digitalRead(modePin) )
    {
      ++buttonHighCount;
      
      // If we've held the button down for long enough, go back to log mode
      
      if( buttonHighCount > 1 )
      {
        // if we're in merge mode, we need to copy the data back
        if( runMode == kMERGE )
        {
           eraseJourney( currentStartAddr );
           copyJourney( k_x_startAddr, currentStartAddr );
        }
        
        journeyStarted = false;
        reset_counters();
        runMode = kRUN;
      }
    }
    else
    {
      buttonHighCount = 0;
    }
  }
 
}

// \}


unsigned long lastSampleIndex = -1;
unsigned long nextTach;

// Calculates the expected number of tacks at this interval
void updateExpected()
{
   unsigned long sampleIndex = floor((loopTime - journeyStartTime) / float(logInterval));
   float sampleFraction = (float(loopTime - journeyStartTime) / float(logInterval)) - sampleIndex;
     
   if( sampleIndex != lastSampleIndex )
   {
      lastSampleIndex = sampleIndex;
      
      const unsigned long sampleAddr = currentStartAddr+sampleIndex;
      if( sampleAddr < currentEndAddr ){
        expectedTach += flash.readByte(sampleAddr);
        nextTach = expectedTach + flash.readByte(sampleAddr+1);
      }
      else
      {
         nextTach = expectedTach; 
      }
   }   

   expectedInterpolatedTach = ( expectedTach * ( 1.0f - sampleFraction ) ) + ( nextTach * sampleFraction );   
}


void recordProgress(unsigned long sampleIndex)
{
   byte sampleDelta = totalTach - lastSampleTach;
   lastSampleTach = totalTach;
   
   unsigned long sampleAddr = currentStartAddr + sampleIndex;
   
   if( sampleAddr > currentEndAddr ) { return; }

   if( runMode == kMERGE )
   {
      byte existingDelta = flash.readByte(sampleAddr);
      sampleDelta = ( sampleDelta + existingDelta ) / 2;
      // Make sure we write into our tmp buffer too
      sampleAddr = k_x_startAddr + sampleIndex;
   }
      
   flash.writeByte(sampleAddr, sampleDelta);
}



// \name State Reporting
// \{

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

// \}


// \name FLASH Utility
// \{

void eraseJourney( unsigned long addr )
{
  for( unsigned int i=0; i<journeyBlockCount; ++i)
  {
     flash.blockErase4K( addr+(4096*i) );
  }
}


void copyJourney( unsigned long fromAddr, unsigned long toAddr )
{
  unsigned long buffer;
  unsigned long byteCount = (journeyBlockCount * 4096) / 4;
  for( unsigned long i=0; i<byteCount; ++i ) 
  {
     flash.readBytes( fromAddr+(i*4), &buffer, 4 );
     flash.writeBytes( toAddr+(i*4), &buffer, 4 );
  }
}

// \}



void reset_counters()
{
    totalTach = 0;
    expectedTach = 0;
    journeyStartTime = loopTime;
    buttonHighCount = 0;
}


