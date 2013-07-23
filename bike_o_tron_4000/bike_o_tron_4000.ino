

// Pin mappings

int redLED = 12;
int greenLED = 13;

int tachPin = 2;
int modePin = 3;

// globals

int tachState = 0;
int lastTachState = 0;
unsigned long totalTachs = 0;

// timings
int timer = 0;
int sampleInterval = 10;

//tmp distance log
int tachInterval = 500;
int resetInterval = 1000;
unsigned long expectedTach = 0;

void setup()
{
  //Serial.begin( 9600 );
  
  pinMode( redLED, OUTPUT );
  pinMode( greenLED, OUTPUT ); 
  
  pinMode( tachPin, INPUT );
  pinMode( modePin, INPUT );
}


void loop() 
{
  //debug();
  
  checkTach();
  
  if( (timer % tachInterval) == 0 ) { ++expectedTach; }
 
  ++timer;
 
  if( (timer % sampleInterval) == 0 )
  {
    int deltaTach = expectedTach - totalTachs;
    reportState( deltaTach );
  }

  if( timer == resetInterval )
  {
     timer = 0; 
  }
  
  delay(1);
}

void checkTach()
{
    tachState = digitalRead( tachPin );
    if( lastTachState == true && tachState == false )
    { 
       ++totalTachs;
    }
    lastTachState = tachState;
}


bool reportState( int delta )
{
    int redState = map( delta, -20, 5, 0, 200 );
    int greenState = map( delta, -5, 20, 255, 0 );
    
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
  Serial.print( "timer: " );
  Serial.print( timer );
  Serial.print( " expectedTach: " );
  Serial.print( expectedTach );
  Serial.print( " tach: " );
  Serial.print( totalTachs );
  Serial.print( "\n" );

}


