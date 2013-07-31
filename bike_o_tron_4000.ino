#include <SPI.h>
#include <SPIFlash.h>

// Pin mappings

int redLED = 10;
int greenLED = 9;

int tachPin = 7;
int modePin = 3;

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

// First 48 bytes reserved for mapping tables
unsigned long k_a_end = 0x00;
unsigned long k_b_end = 0x04;
unsigned long k_c_end = 0x08;
unsigned long k_d_end = 0x0c;
unsigned long k_x_end = 0x10;

// Memory Indexes of journey starts, we use 24x4k blocks per journey
unsigned long k_a_startAddr = 0x00000 + 4096;
unsigned long k_b_startAddr = k_a_startAddr + journeyBlockCount * 4096;
unsigned long k_c_startAddr = k_b_startAddr + journeyBlockCount * 4096;
unsigned long k_d_startAddr = k_c_startAddr + journeyBlockCount * 4096;
unsigned long k_x_startAddr = k_d_startAddr + journeyBlockCount * 4096;


void setup()
{
  Serial.begin( 9600 );
  
  pinMode( redLED, OUTPUT );
  pinMode( greenLED, OUTPUT ); 
  
  pinMode( tachPin, INPUT );
  attachInterrupt(0, tach_interrupt, RISING);

  pinMode( modePin, INPUT );
  detachInterrupt(1);
  
  if (flash.initialize())
  {
    Serial.println("Init OK!");
  } else {
    Serial.println("Init FAIL!");
  }
}


void tach_interrupt()
{
  // We'll need to look at debouncing when we
  // have the real wheel hardware
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
     Serial.print( "Staring Journey" );

     // Store the current start address of the journey
     if      ( journeyIndex == 0 ) { currentStartAddr = k_a_startAddr; }
     else if ( journeyIndex == 1 ) { currentStartAddr = k_b_startAddr; }
     else if ( journeyIndex == 2 ) { currentStartAddr = k_c_startAddr; }
     else                          { currentStartAddr = k_d_startAddr; }
     
     if( runMode == kLOG )
     {
        eraseJourney( currentStartAddr );
     }
     else
     {  
        // read journey end addr (long) start at 0
        currentEndAddr = currentStartAddr + (4096 *journeyBlockCount);
     }
     reset_counters();
     journeyStarted = true;
  }
  
  lastRunLoopTime = loopTime;
}


void eraseJourney( unsigned long addr )
{
  for( unsigned int i=0; i<journeyBlockCount; ++i)
  {
     flash.blockErase4K( addr+(4096*i) );
  }
}

void reset_counters()
{
    totalTach = 0;
    expectedTach = 0;
    journeyStartTime = loopTime;
    buttonHighCount = 0;
}


void loop_journey() 
{
  if( ( loopTime - lastRunLoopTime ) < updateInterval ) { return; }
    
  // The Logic
  //
  // At each sample interval, we compare expectedTachs to totalTacks
  // and map the delta to the color range of the output LED
  updateExpected();
  
  reportTachState( expectedInterpolatedTach - (totalTach + tachOffset) );
  
  lastRunLoopTime = loopTime;
}


bool ledOn = false;
void loop_log()
{
  if(ledOn && (( loopTime - lastLogLoopTime ) > 100 ))
  {
      analogWrite(redLED, 0);  
  }
  
  
  if( (loopTime - lastLogLoopTime) > logInterval )
  {
    lastLogLoopTime = loopTime;
    recordProgress(sampleIndex);
     ++sampleIndex;

    if( digitalRead(modePin) )
    {
      ++buttonHighCount;
      if( buttonHighCount > 1 )
      {
         // store the end address
         const unsigned long endAddr = currentStartAddr + sampleIndex;
         flash.writeBytes( journeyIndex*4, &endAddr, 4 );
         journeyStarted = false;
         reset_counters();
      }
    }
    else
    {
      buttonHighCount = 0;
    }
    analogWrite(redLED, 255);
    ledOn = true;
  }
 
}


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

   expectedInterpolatedTach = ( expectedTach * (1.0f - sampleFraction) ) + ( nextTach * sampleFraction );   
}


void recordProgress(unsigned long sampleIndex)
{
   byte sampleDelta = totalTach - lastSampleTach;
   lastSampleTach = totalTach;
   const unsigned long sampleAddr = currentStartAddr+sampleIndex;
   if( sampleAddr < currentEndAddr ) {
     flash.writeByte(sampleAddr, sampleDelta);
   }
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



