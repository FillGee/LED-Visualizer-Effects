#include "LedControl.h"
#include "binary.h"
#include "FastLED.h"
//#include "AdafruitIO_Ethernet.h"

//Declare Spectrum Shield pin connections
#define STROBE 4
#define RESET 5
#define AUDIO_IN A5
#define DELAY 25

/*
//define the pins to drive the matrix so we can change them later if needed
#define DATA 12
#define CS 11
#define CLK 10
*/

//define all the FastLED strip info so its here to change if needed. Updated to use teensy 4.0 parallel output stuff
#define STRIP_DATA 14
#define NUM_STRIPS 1
#define NUM_LEDS_PER_STRIP 300
#define NUM_LEDS 300
#define STRIP_TYPE WS2812B
#define COLOR_ORDER GRB

/* COMMENTED OUT UNTIL TEENSY
//Adafruit IO setup stuff
#define IO_USERNAME "FillGee"
#define IO_KEY "5afc1d57eb30409283b0565a088a797b"
AdafruitIO_Ethernet io(IO_USERNAME, IO_KEY);
AdafruitIO_Feed *power = io.feed("on-status"); //tell the arduino to look at the on-status feed anytime we say "power"
bool enabled = false; //setup a boolean flag for the power status, and set it to be off to start.
*/

CRGBArray<NUM_LEDS_PER_STRIP * NUM_STRIPS> stripLEDs; //make an array with an entry for each LED on the strip, of the type CRGB which is used by FastLED

//define the column "intensities" so we dont need to type them out every time
//byte columnFill[] = {B00000000, B00000001, B00000011, B00000111, B00001111, B00011111, B00111111, B01111111, B11111111};
//byte columnMax[] = {B00000000, B00000001, B00000010, B00000100, B00001000, B00010000, B00100000, B01000000, B10000000};

//LedControl lc=LedControl(DATA,CLK,CS,1);

int freq;
int Frequencies_One[7];
int audioAmplitudes[7];
int intensity;
int i;
int matrixRow;
int matrixColumn;

//testing out looking at max volumes and when the music should "bump"
//shamelessly stolen from https://github.com/bartlettmic/SparkFun-RGB-LED-Music-Sound-Visualizer-Arduino-Code/blob/master/Visualizer_Program/Visualizer_Program.ino

#define BUMP_VALUE 375 //have a constant for the value that the audio needs to exceed in order for it to be a bump. Works best around 300-400
#define BUMP_DIFFICULTY .925 //the difficulty on a scale of 1-10 that a new bump will trigger as one based on average bumps in the past. should be .9-1.0
#define MINVOLUME 750 //bumps also have to be at least this volume in order to trigger effects. Probably should be 700+
int maxAmplitudes[7]; //will hold the highest recorded value of each frequency for comparison
int avgAmplitudes[7]; //will hold and update the average recorded value of each frequency, so we can tell when its a lot different
float avgBumps[] = {BUMP_VALUE, BUMP_VALUE, BUMP_VALUE, BUMP_VALUE, BUMP_VALUE, BUMP_VALUE, BUMP_VALUE}; //hold the average bump so far in each frequency, so we can compare to the normal bumps and see if it should really be counted
int lastAmplitudes[] = {0, 0, 0, 0, 0, 0, 0}; //holds the previous values of each frequency so we can compare to the current
bool bumps[] = {false, false, false, false, false, false, false}; //have a boolean for if each frequency is experiencing a bump

CRGB avgColor;
CRGB oldAvgColor;
CRGB currentFlash;

void setup() 
{
  // put your setup code here, to run once:
    pinMode(STROBE, OUTPUT);
    pinMode(RESET, OUTPUT);
    pinMode(AUDIO_IN, INPUT);  
    digitalWrite(STROBE, HIGH);
    digitalWrite(RESET, HIGH);
    //Serial.begin(115200);
    
    //Initialize Spectrum Analyzers
    digitalWrite(STROBE, LOW);
    delay(1);
    digitalWrite(RESET, HIGH);
    delay(1);
    digitalWrite(STROBE, HIGH);
    delay(1);
    digitalWrite(STROBE, LOW);
    delay(1);
    digitalWrite(RESET, LOW);

     /*
    The MAX72XX is in power-saving mode on startup,
    we have to do a wakeup call
    */
    //lc.shutdown(0,false);
    /* Set the brightness to a medium values */
    //lc.setIntensity(0,5);
    /* and clear the display */
    //lc.clearDisplay(0);

    FastLED.addLeds<NUM_STRIPS, WS2812B, STRIP_DATA, COLOR_ORDER>(stripLEDs, NUM_LEDS); //constructs the led strip with FastLED so it knows how to drive it
    FastLED.setBrightness(255); //set the brightness globally, everything else will be a percentage of this?
    FastLED.clear();
    FastLED.show();

    /* COMMENTED OUT UNTIL TEENSY
    //setup the Adafruit IO connection
    Serial.println(F("Connecting to Adafruit IO"));
    io.connect();

    //while waiting for the connection just type dots so we know its waiting
    while(io.status() < AIO_CONNECTED) 
    {
      Serial.print(F("."));
      delay(500);
    }
    //once connected we print the status?
    Serial.println(io.statusText());

    power->onMessage(handlePower); //when "power" (on-status) gets new data, it passes to the handlePower() function below
    power->get(); //read the current value in the feed and handle it
    */
}


/*************Pull frquencies from Spectrum Shield****************/
void readFrequencies(int freq)
{
  //Read frequencies for each band
    audioAmplitudes[freq] = analogRead(AUDIO_IN);
    digitalWrite(STROBE, HIGH);
    delayMicroseconds(75);                    // Delay necessary due to timing diagram 
    digitalWrite(STROBE, LOW);
    delayMicroseconds  (100);                    // Delay necessary due to timing diagram 
}

void detectBumps(int freq)
{
    if (audioAmplitudes[freq] > maxAmplitudes[freq]) //if the new value is greater than our previous maximum
    {
      maxAmplitudes[freq] = audioAmplitudes[freq];  //set the maximum amplitude for this band to the current one
    }

    if (audioAmplitudes[freq] - lastAmplitudes[freq] > BUMP_VALUE) //if the difference between the last amplitude and the current one is greater than 2 LEDs, record its size in the avgBumps
    {
      avgBumps[freq] = ((avgBumps[freq] + (audioAmplitudes[freq] - lastAmplitudes[freq])) / 2.0); //take the previous average, add the current jump and divide by 2 for the new avgBump
    }

    //detect "bumps"
    if ((((audioAmplitudes[freq] - lastAmplitudes[freq]) > (avgBumps[freq] * BUMP_DIFFICULTY))) && (audioAmplitudes[freq] > MINVOLUME)) //have it for bumps or just for loudness
    {
      bumps[freq] = true; //say that we have a bump in a certain frequency
    }
    //after we have done the math, put these values in last so we can use them next loop
    lastAmplitudes[freq] = audioAmplitudes[freq];
}

/*
void matrixGraph(int i)
{
    intensity = audioAmplitudes[i] / 125; //we have 8 LEDs vertically to show the intensity of each band. 1000/8 is 125, so each 125 change in intensity will light up another led vertically
    //lc.setColumn(0, i, columnMax[intensity]);
    if (bumps[i])
    {
      //lc.setColumn(0, 7, columnFill[8]);
    }
    else
    {
      //lc.setColumn(0, 7, columnFill[0]);
    }
}
*/

void addSingleColorPixel()
{
   //find the frequencies with the highest amplitude and just calculate the color based on that?
  int maximum = 0;
  int secondMax = 0;
  int thirdMax = 0;
  int maxIndex = 0;
  int secondMaxIndex = 0;
  int thirdMaxIndex = 0;
  
  for (int i=0; i<7; i++)
  {
    if (audioAmplitudes[i] > maximum)
    {
      thirdMax = secondMax;
      thirdMaxIndex = secondMaxIndex;
      //put the old max value in secondMax so we can get the second highest value
      secondMax = maximum;
      secondMaxIndex = maxIndex;
      //store this new max value
      maximum = audioAmplitudes[i];
      maxIndex = i;
    }
  }


  oldAvgColor = avgColor; //take the most recent average color and store it here so we can use it to blend
  avgColor = CRGB(0,0,0); //reset the average color to nothing so we can add stuff from scratch
  
  calculateColor(maxIndex, maximum, 0.7); //the maximum freqeuncy gets to set the color with .70 effectiveness
  calculateColor(secondMaxIndex, secondMax, 0.35); //the second to the max gets to add their color,but only .35 effectiveness
  calculateColor(thirdMaxIndex, thirdMax, 0.25); //lastly the third to max adds theirs with .25 effectiveness

  avgColor = blend(oldAvgColor, avgColor, 128); //we can take the color between the last color and the current one to get more of a smooth gradient between colors instead of jumps from red > green etc
  moveRight(1);
  stripLEDs[0] = CRGB(avgColor.red, avgColor.green, avgColor.blue); //sets the first pixel to the "average" color of the song at that moment. Then use move right to move it down the chain
  //fill_solid(stripLEDs,NUM_LEDS, CRGB(avgColor.red, avgColor.green, avgColor.blue)); //sets the whole strip to the "average" color, which will then flash to the next when its calculated.
  //fill_gradient_RGB(stripLEDs, 0, avgColor, NUM_LEDS-1, oldAvgColor); //should make a gradient from the new color on the start to the old color on the end? Doesnt seem to work
  FastLED.show();
  delayMicroseconds(75);
}

void calculateColor(int band, int value, float multiplier)
{
  int topValue = 220;
  int twoThirds = 150;
  int oneThirds = 90;
  float blueShift = 0.70; //literally just turn the blue values to this percent so its not so overpowering
  float greenShift = 1.8; //increase the heck out of greens because theres not enough of them usually
  float redShift = 0.70; //red multiplier, could maybe turn it down a tad but I like it
  
  switch(band)
  {
    case 0:
      avgColor.red = qadd8(avgColor.red , (map(value, 0, 1024, 0, (topValue * redShift)) * multiplier));
      break;
    case 1:
      avgColor.red = qadd8(avgColor.red , (map(value, 0, 1024, 0, (twoThirds * redShift)) * multiplier));
      avgColor.blue = qadd8(avgColor.blue , (map(value, 0, 1024, 0, (oneThirds * blueShift)) * multiplier));
      break;
    case 2:
      avgColor.red = qadd8(avgColor.red , (map(value, 0, 1024, 0, (oneThirds * redShift)) * multiplier));
      avgColor.blue = qadd8(avgColor.blue , (map(value, 0, 1024, 0, (twoThirds * blueShift)) * multiplier));
      break;
    case 3:
      avgColor.blue = qadd8(avgColor.blue , (map(value, 0, 1024, 0, (topValue * blueShift)) * multiplier));
      avgColor.green = qadd8(avgColor.green , (map(value, 0, 1024, 0, (oneThirds * greenShift)) * multiplier)); //even though this should only be blue, add some green otherwise the green end is very under-utilized
      break;
    case 4:
      avgColor.blue = qadd8(avgColor.blue, (map(value, 0, 1024, 0, (twoThirds * blueShift)) * multiplier));
      avgColor.green = qadd8(avgColor.green , (map(value, 0, 1024, 0, (oneThirds * greenShift)) * multiplier));
      break;
    case 5:
      avgColor.blue = qadd8(avgColor.blue , (map(value, 0, 1024, 0, (oneThirds * blueShift)) * multiplier));
      avgColor.green = qadd8(avgColor.green , (map(value, 0, 1024, 0, (twoThirds * greenShift)) * multiplier));
      break;
    case 6:
      avgColor.green = qadd8(avgColor.green , (map(value, 0, 1024, 0, (topValue * greenShift)) * multiplier));
      break;
  }
}

void moveRight(int pixels)
{
  for (int j = NUM_LEDS-1; j>=pixels; j--)
  {    
    stripLEDs[j] = stripLEDs[j-pixels];
  }
  FastLED.show();
  delayMicroseconds(75);
}

void moveLeft(int pixels)
{
  for (int j = 0; j< NUM_LEDS+pixels; j++)
  {    
    stripLEDs[j] = stripLEDs[j+pixels];
  }
  FastLED.show();
  delayMicroseconds(75);
}

void sevenBandEQFromMid()
{
  stripLEDs.fadeToBlackBy(25);
  for (int i = 0; i < 7; i++)
  {
    readFrequencies(i);
    detectBumps(i);
    //matrixGraph(i);
    int bandLength = NUM_LEDS / 7;
    intensity = map(audioAmplitudes[i], 150, 1024, 0, bandLength);
    int saturation = 220;
    for (int j = ((i * bandLength) + (bandLength / 2)); j < ((i+1) * bandLength); j++) //start in the middle of each band length and go to the start of the next
    {
      if (j < ((i * bandLength) + (bandLength / 2) + (intensity / 2)) || bumps[i])
      {
        stripLEDs[j] = CHSV(i*30, saturation, 180);
        saturation++;
      }
      else
      {
        stripLEDs[j].nscale8(196); //fade by 75%
        //stripLEDs[j] = CHSV(0, 0, 0); 
      }
    }
    saturation = 220;
    for (int j = ((i * bandLength) + (bandLength / 2)); j > (i * bandLength); j--) //start in the middle of each band length and go backwards to the start of the band
    {
      if (j > ((i * bandLength) + (bandLength / 2) - (intensity / 2)) || bumps[i])
      {
        stripLEDs[j] = CHSV(i*30, saturation, 180);
        saturation++;
      }
      else
      {
        stripLEDs[j].nscale8(196);
        //stripLEDs[j] = CHSV(0, 0, 0); 
      }
    }
    bumps[i] = false;
  }
  FastLED.show();
  delayMicroseconds(75);
}

void sevenBandEQ()
{
  stripLEDs.fadeToBlackBy(25);
  for (int i = 0; i < 7; i++)
  {
    readFrequencies(i);
    detectBumps(i);
    //matrixGraph(i);
    int bandLength = NUM_LEDS / 7;
    intensity = map(audioAmplitudes[i], 150, 1024, 0, bandLength);
    int saturation = 200;
    int brightness = 200;
    for (int j = ((i * bandLength)); j < ((i+1) * bandLength); j++) //start in the middle of each band length and go to the start of the next
    {
      if (j < ((i * bandLength) + intensity) || bumps[i])
      {
        stripLEDs[j] = CHSV(i*30, saturation, brightness);
        saturation++;
        brightness++;
      }
      else
      {
        stripLEDs[j].nscale8(192);
        //stripLEDs[j] = CHSV(0, 0, 0); 
      }
    }
    bumps[i] = false;
  }
  FastLED.show();
  delayMicroseconds(75);
}

void addBumpPixel(int i) //broke this out so I can do more complicated stuff to it later
{
  if (bumps[i])
  {  
    moveRight(1);
    stripLEDs[0] = CHSV(i*36, 200, 255);
    FastLED.show();
    bumps[i] = false;
    delayMicroseconds(75);
  }
}

void bumpsOnlyLinear()
{
  for (int i = 0; i < 7; i++)
  {
    readFrequencies(i);
    detectBumps(i);
    //matrixGraph(i);
    addBumpPixel(i);
  }
}

void linearSpectrumNoBumps()
{
  for (int i=0; i<7; i++)
  {
    readFrequencies(i);
    //matrixGraph(i);
  }
  addSingleColorPixel();
}

void linearSpectrumWithBumps()
{
  for (int i=0; i<7; i++)
  {
    readFrequencies(i);
    detectBumps(i);
    //matrixGraph(i);
    addBumpPixel(i);   
  }
  addSingleColorPixel();
  delayMicroseconds(75);
}

void bumpsOnlyFlashesToNeutral()
{
  for (int i=0; i<7; i++)
  {
    readFrequencies(i);
    detectBumps(i);
    //matrixGraph(i);
    if (bumps[i])
    {
      fill_solid(stripLEDs, NUM_LEDS, CHSV(i*36, 255, 150));
      currentFlash = CHSV(i*36, 255, 150);
      bumps[i] = false;
    }
    else
    {
      fill_solid(stripLEDs, NUM_LEDS, blend(CHSV(0,0,150), currentFlash, 250));
      currentFlash = blend(CHSV(0,0,150), currentFlash, 250);
    }
    FastLED.show();
    delayMicroseconds(75);
  }
}

void bumpsOnlyFlashesToBlack()
{
  for (int i=0; i<7; i++)
  {
    readFrequencies(i);
    detectBumps(i);
    //matrixGraph(i);
    if (bumps[i])
    {
      fill_solid(stripLEDs, NUM_LEDS, CHSV(i*36, 225, 125));
      bumps[i] = false;
      FastLED.show();
    }
    else
    {
      stripLEDs.fadeToBlackBy(20);
      FastLED.show();
    }
    delayMicroseconds(75);
  }
}

void rainbowGradientBumpStop()
{
  for (int hue = 0; hue < 256; hue++)
  {
    for (int i=0; i<7; i++)
    {
      readFrequencies(i);
      detectBumps(i);
      //matrixGraph(i);
      if (bumps[i])
      {
        //fill_solid(stripLEDs, NUM_LEDS, CHSV(i*36, 255, 150));
        bumps[i] = false;
        //FastLED.show();
        //FastLED.delay(40);
        hue = hue - 2;
        //hue = hue + 4;
      }
    }
    for (int i=0; i<NUM_LEDS; i++)
    {
      stripLEDs[i] = CHSV(i+hue,255,150);
    }
    FastLED.show();
    delayMicroseconds(75);
  } 
}

/* COMMENTED OUT UNTIL WE GET THE TEENSY 
void handlePower(AdafruitIO_Data *data) //the function that is called anytime there is new data in the "on-status" feed
{
  Serial.println(data->toString());
  if (data->toString() == "ON")
  {
    enabled = true;
  }
  else
  {
    enabled = false;
  }
}
*/
void simpleChase()
{
  int lit = 3;
  int space = 3;
  for (int j=0; j<space+lit; j++)
  {
    for (int i=0; i < (NUM_LEDS-1) - lit - space- j; i=i+lit+space)
    {
      stripLEDs(i+j, i+j+lit-1) = CHSV(triwave8(i+j), 150, triwave8(i+j));
      stripLEDs(lit+j, j+lit+space-1) = CHSV(0, 0, 0);
    }
    FastLED.show();
    //delayMicroseconds(75);
    delay(35);
    FastLED.clear();
  }
  /*
  for (int j=0; j<255; j++)
  {
  for (int i=0; i<NUM_LEDS; i++)
  {
    stripLEDs[i] = CHSV(sin8(i+j), 255, cos8((i+j)*128));
    //FastLED.show();
  }
  delay(75); //50 works but is too quick. 75 should prob be the minimum
  //
  }
  */
  
}

void bounce()
{
  uint8_t spd = 1;
  for (uint16_t i=0; i<NUM_LEDS-spd; i+=spd)
  {
    fadeToBlackBy(stripLEDs, NUM_LEDS, (64 /  spd));
    stripLEDs[i] = CHSV((triwave8(i)), random8(150,255), 150);
    FastLED.show();
    //FastLED.clear();
    delayMicroseconds(75);
  }
  for (uint16_t j=NUM_LEDS-1; j>=0+spd; j-=spd)
  {
    fadeToBlackBy(stripLEDs, NUM_LEDS, (64 / spd));
    stripLEDs[j] = CHSV((triwave8(j)), random8(150,255), 150);
    FastLED.show();
    //FastLED.clear();
    delayMicroseconds(75);
  }
}

void bounceUsingWaves() //neat lightweight implementation using the triwave function and mapping it to our length. 
{
  for (uint8_t i=0; i<256; i++)
  {
    stripLEDs[map(triwave8(i), 0, 255, 0, NUM_LEDS-1)] = CHSV(sin8(i), 255, 150);
    FastLED.show();
    delayMicroseconds(100);
    FastLED.clear();
  }
}

//testing the sin8 and cos8 values which go from 0-255. Sin 64 =255. Sin 192 = 1. Period of 128? cos8(0) = 255, cos8(128) = 1, cos8(256) = 255.
//triwave is perfectly linear, period of 128 with values of 0-255. One increase in i = 2 increase in output. triwave(0) = 0
void testSin()
{
  for (int i=0; i<NUM_LEDS; i++)
  {
    Serial.print("Current i value: ");
    Serial.print(i);
    Serial.print(". Triwave of i value: ");
    Serial.println(triwave8(i));
  }
  delay(9999999999999999999999999999999999999999999999999);
}

void lighthouse()
{
  
}


void loop() 
{
  //io.run();
  /***Pick ONLY ONE of the following effects to run *************/
  //bumpsOnlyFlashesToNeutral();
  //bumpsOnlyFlashesToBlack();
  //rainbowGradientBumpStop();
  //linearSpectrumNoBumps();
  //linearSpectrumWithBumps();
  //bumpsOnlyLinear();
  //sevenBandEQFromMid();
  //sevenBandEQ();
  simpleChase();
  //bounceUsingWaves();
  //bounce();
  //testSin();
}
