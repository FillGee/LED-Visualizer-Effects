#define FASTLED_USE_GLOBAL_BRIGHTNESS 1
#include <FastLED.h>
#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>



#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h> 


AudioInputI2S            i2s1;           
AudioAnalyzePeak         peak1;          
AudioAnalyzeFFT1024      myFFT;      
AudioOutputI2S           i2s2;

AudioConnection          patchCord1(i2s1, 1, peak1, 0);
AudioConnection          patchCord2(i2s1, 0, myFFT, 0);
AudioConnection          patchCord3(i2s1, 0, i2s2, 0);
AudioConnection          patchCord4(i2s1, 1, i2s2, 1);

AudioControlSGTL5000 audioShield; 


// ALL THE ETHERNET PUBSUB DATA TO CONNECT US TO THE MQTT FEEDS. DOCUMENTATION HERE: https://pubsubclient.knolleary.net/api.html#publish1
#define AIO_USERNAME  "FillGee"
#define AIO_KEY ""
#define SERVER "io.adafruit.com"
#define PORT 1883
#define FEED1 "FillGee/feeds/on-status"
#define FEED2 "FillGee/feeds/effect"
#define FEED3 "FillGee/feeds/led-brightness"
#define FEED4 "FillGee/feeds/current-palette"
#define FEED5 "FillGee/feeds/single-color"
#define FEED6 "FillGee/feeds/speed"
#define FEED7 "FillGee/feeds/threshold"
byte mac[]    = {  0xDE, 0xED, 0xBA, 0xFE, 0xFE, 0xED }; //setup a generic MAC adress for the teensy
//IPAddress ip(192, 168, 0, 182); //in case we dont want to use DHCP we can have it connect under a certain IP address
EthernetClient ethClient; //initialize an ethernet connection to the internet
PubSubClient AIOClient(ethClient); //initialize a pubsubclient connection using the ethernet connection, called AIOClient

bool updates; //make a boolean flag to let functions know if they should return and re-calculate or not
bool powered; //make a boolean flag to hold the "on-status" of the LEDs
String ctrl_effect; //make a string to hold the current effect
int ctrl_brightness; //make an int (0-100) to hold the brightness percentage
String ctrl_palette; //make a string to hold the palette status
//string color //should be a hex thing, research 
int ctrl_speed; //make an int (0-100) to hold the speed of effects
int ctrl_threshold;

//define all the FastLED strip info so its here to change if needed. Updated to use teensy 4.0 parallel output stuff
#define STRIP_DATA 14
#define CLOCK_PIN 18
#define NUM_STRIPS 1
#define NUM_LEDS_PER_STRIP 144
#define NUM_LEDS 144
#define STRIP_TYPE APA102
#define COLOR_ORDER BGR


CRGBArray<NUM_LEDS_PER_STRIP> stripLEDs; //make an array with an entry for each LED on the strip, of the type CRGB which is used by FastLED

/****************THIS WILL ALL BE DELETED NOW THAT THE MSGEQ7s ARE NO LONGER USED IN FAVOR OF ADC AUDIO PROCESSING ********************************/ 
int freq;
int Frequencies_One[7];
int audioAmplitudes[7];
int intensity;
int i;
int matrixRow;
int matrixColumn;

int channels = 16; //we could change this to a higher number, but 16 works well for what my goals are
float level[16]; //array to hold the 16 frequency bands
int peakLevel; //int to hold the current peak/loudness level
float averageFreq; //float to hold the "average" frequency
float totalLevel;

//testing out looking at max volumes and when the music should "bump"
//based on https://github.com/bartlettmic/SparkFun-RGB-LED-Music-Sound-Visualizer-Arduino-Code/blob/master/Visualizer_Program/Visualizer_Program.ino

#define BUMP_VALUE 375 //have a constant for the value that the audio needs to exceed in order for it to be a bump. Works best around 300-400
#define BUMP_DIFFICULTY .925 //the difficulty on a scale of 1-10 that a new bump will trigger as one based on average bumps in the past. should be .9-1.0
#define MINVOLUME 750 //bumps also have to be at least this volume in order to trigger effects. Probably should be 700+
int maxAmplitudes[7]; //will hold the highest recorded value of each frequency for comparison
int avgAmplitudes[7]; //will hold and update the average recorded value of each frequency, so we can tell when its a lot different
float avgBumps[] = {BUMP_VALUE, BUMP_VALUE, BUMP_VALUE, BUMP_VALUE, BUMP_VALUE, BUMP_VALUE, BUMP_VALUE}; //hold the average bump so far in each frequency, so we can compare to the normal bumps and see if it should really be counted
int lastAmplitudes[] = {0, 0, 0, 0, 0, 0, 0}; //holds the previous values of each frequency so we can compare to the current
bool bumps[] = {false, false, false, false, false, false, false}; //have a boolean for if each frequency is experiencing a bump

/****************THIS WILL ALL BE DELETED NOW THAT THE MSGEQ7s ARE NO LONGER USED IN FAVOR OF ADC AUDIO PROCESSING ********************************/

CRGB avgColor;
CRGB oldAvgColor;
CRGB currentFlash;

int oldHueDiff[300];
int currentHueDiff[300];
uint8_t baseHue;

//noise random numbers and variables
int scale = 100; //makes the noise more... noisy
int difference = 85; //the maximum difference in hues that the "noise" can have from the base hue
int randomX;
int randomY;
int randomZ;

//bounce variables that we dont want to update every time
int bounceLocation = 0;
int bounceDirection = 1;

//disolve function array for randomization, variables
int pixelOrder[300];

CRGBPalette16 currentPalette;

void setup()
{
    AudioMemory(12);
    audioShield.enable();
    audioShield.inputSelect(AUDIO_INPUT_LINEIN);
    audioShield.volume(0.7);
    //myFFT.windowFunction(AudioWindowHanning1024);
  
    FastLED.addLeds<STRIP_TYPE, STRIP_DATA, CLOCK_PIN, COLOR_ORDER, DATA_RATE_MHZ(12)>(stripLEDs, NUM_LEDS); //constructs the led strip with FastLED so it knows how to drive it
    FastLED.setBrightness(128); //set the brightness globally, everything else will be a percentage of this? APAs are stupid bright, clamp them down!
    
    FastLED.clear();
    FastLED.show();


    AIOClient.setServer(SERVER, PORT); //connect to the MQTT server and port defined above
    AIOClient.setCallback(handleFeeds); //set the function that will be called anytime that the MQTT client recieves data. In this case, the funciton handleFeeds()
    Ethernet.begin(mac); //start the internet connection with the MAC adress above. Add a comma and "ip" to not use DHCP
    delay(1500); //Allow the hardware to sort itself out
    
    FastLED.clear();
    FastLED.show();

    randomX = random16(); //generate random numbers for the noise function
    randomY = random16(); //generate random numbers for the noise function
    randomZ = random16(); //generate random numbers for the noise function
    arrayShuffle(); //generate a random array of indices for the amount of leds we have. Do this on setup so when dissolve is called the first time it doesnt lag

    AIOClient.connect("teensyClient", AIO_USERNAME, AIO_KEY); //connect to the AIO feeds so we can write the default states to them so we dont have de-sync
    powered = true; //initially powered
    AIOClient.publish("FillGee/feeds/on-status", "ON"); //send the initial power state to MQTT so we dont have a de-sync on the dashboard
    ctrl_brightness = 40;
    AIOClient.publish("FillGee/feeds/led-brightness", "40"); //set initial brightness to 90 and send that over MQTT
    ctrl_effect = "Noise";
    AIOClient.publish("FillGee/feeds/effect", "Noise"); //set initial effect to noise since its the most interesting and sync with MQTT
    currentPalette = RainbowColors_p;
    AIOClient.publish("FillGee/feeds/current-palette", "RainbowColors_p"); //set initial palette to rainbow colors as its the most visually interesting and sync with MQTT
    ctrl_speed = 50;
    AIOClient.publish("FillGee/feeds/speed", "50");
    ctrl_threshold = 20;
    AIOClient.publish("FillGee/feeds/threshold", "20");
    updates = false;
    
    Serial.println("Connected to Adafruit IO and set initial values!");
    AIOClient.disconnect(); //dont know why this is needed, but if we dont have it the MQTT updates never come through. I guess between publishing and subscribing it needs to disconnect?
    Serial.println("Disconnecting from Adafruit IO now that initial values have been set.");
}


//read the 512 FFT frequencies into 16 levels. Higher octaves need combining of many bins otherwise low bins always "win"
void readAudioData()
{
    if (myFFT.available()) 
    {
      level[0] =  myFFT.read(0) * 100; //multiplying by 100 gets the numbers in an easier to understand values. Total bins will add up to 100?, individual levels will be 0-100
      level[1] =  myFFT.read(1) * 100;
      level[2] =  myFFT.read(2, 3) * 100;
      level[3] =  myFFT.read(4, 6) * 100;
      level[4] =  myFFT.read(7, 10) * 100;
      level[5] =  myFFT.read(11, 15) * 100;
      level[6] =  myFFT.read(16, 22) * 100;
      level[7] =  myFFT.read(23, 32) * 100;
      level[8] =  myFFT.read(33, 46) * 100;
      level[9] =  myFFT.read(47, 66) * 100;
      level[10] = myFFT.read(67, 93) * 100;
      level[11] = myFFT.read(94, 131) * 100;
      level[12] = myFFT.read(132, 184) * 100;
      level[13] = myFFT.read(185, 257) * 100;
      level[14] = myFFT.read(258, 359) * 100;
      level[15] = myFFT.read(360, 511) * 100;
      // See this conversation to change this to more or less than 16 log-scaled bands?
      // https://forum.pjrc.com/threads/32677-Is-there-a-logarithmic-function-for-FFT-bin-selection-for-any-given-of-bands

      //calculate the "average" frequency based on the 16 above, but with a range of 1-16
      averageFreq = 0.0;
      totalLevel = 0.1;
      for (int i=0; i<channels; i++)
      {
        if (level[i] > ctrl_threshold) //ignore it if its small, which just makes the data more variable and less midtone averagey
        {
          averageFreq += ((i+1) * level[i]); //really just summing up all the frequencies
          totalLevel += level[i];
        }
      }
      averageFreq = averageFreq / totalLevel;
    
      //Serial.println(totalLevel);
      //Serial.print("Frequency: ");
      //Serial.println(averageFreq);
      //Serial.println();
      //Serial.println(level[1]); //debug to see the values in bin 2
      if (peak1.available())
      {
        peakLevel = peak1.read() * 255.0; //this makes it into an int from 0-255 which correlates nicely to saturation/brightness
        //Serial.print("Peak: ");
        //Serial.println(peakLevel);
      }
    }
}

// NO LONGER NEEDED WITH NEW AUDIO PROCESSING, WILL DELETE LATER
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


void linearAverageFrequency(int bright, int spd)
{
  while (!updates) //just run this in a loop until theres updates, then it can return to the main loop function. However if we dont check for updates within the function we never exit
  {
    AIOClient.loop(); //check the data in the feeds while this is running so we know if we should stop ever.
    EVERY_N_MILLIS_I(delayMs, 15)
    {
      delayMs.setPeriod(map(spd, 0, 100, 30, -1));
      readAudioData(); //update the FFT and peak values to get the averageFreq
      oldAvgColor = avgColor; //take the most recent average color and store it here so we can use it to blend
      int scaledBrightness = map(peakLevel, 0, 255, 0, bright);
      //avgColor = CHSV(averageFreq * 16, 255, scaledBrightness); //make the color based on average frequency and the loudness as the brightness
      avgColor = ColorFromPalette(currentPalette, averageFreq * 16, scaledBrightness, LINEARBLEND);
      
      avgColor = blend(oldAvgColor, avgColor, 128); //we can take the color between the last color and the current one to get more of a smooth gradient between colors instead of jumps from red > green etc
      moveRight(1); //move all the pixels right one before adding the new pixel
  
      stripLEDs[0] = avgColor; //sets the first pixel to the "average" color of the song at that moment. Then use move right to move it down the chain

      FastLED.show();
    }
  } 
}


void moveRight(int pixels)
{
  for (int j = NUM_LEDS-1; j>=pixels; j--)
  {    
    stripLEDs[j] = stripLEDs[j-pixels];
  }
}

void moveLeft(int pixels)
{
  for (int j = 0; j< NUM_LEDS+pixels; j++)
  {    
    stripLEDs[j] = stripLEDs[j+pixels];
  }
}

//makes a n-channel EQ/visualizer from the FFT data. Will update to the current brightness and palette, but speed does nothing. 
void EQ(int bright)
{
  while (!updates)
  {
    AIOClient.loop(); //check the data in the feeds while this is running so we know if we should stop ever.
    readAudioData(); //update the 16 FFT bins
    fadeToBlackBy(stripLEDs, NUM_LEDS, 32); //1/8th fade every loop, "lowers" each segment down and fades them out
    
    for (int i = 0; i < channels; i++)
    {
      int bandLength = NUM_LEDS / channels; //set each band to be 1/16 of the overal strip length
      intensity = map(level[i], 0, 40, 0, bandLength); //the amount of leds to be lit up, 50 is basically the max value any bin can have, 40 seems pretty good
      for (int j = ((i * bandLength)); j < ((i+1) * bandLength); j++) //go from the start of one band to the next one
      {
        if (j < ((i * bandLength) + intensity)) //if the index we are on falls within the "length" of the instensity, it needs to be lit. Otherwise it will be faded in the main loop
        {
          stripLEDs[j] = ColorFromPalette(currentPalette, i * (256 / channels), bright, LINEARBLEND); //set the LED color to be one of the colors you can get from the current palette, with global brightness
        }
      }
    }
    FastLED.show();
    delay(20); //if we dont have enough delay it looks more flickery and annoying
  }
}

//similar to the EQ above, but each band will light from the middle out instead of left>right. 
void EQFromMid(int bright)
{
  while (!updates)
  {
    AIOClient.loop(); //check the data in the feeds while this is running so we know if we should stop ever.
    readAudioData(); //update the 16 FFT bins
    fadeToBlackBy(stripLEDs, NUM_LEDS, 32); //1/8th fade every loop, "lowers" each segment down and fades them out
    
    for (int i = 0; i < channels; i++)
    {
      int bandLength = NUM_LEDS / channels; //set each band to be 1/16 of the overal strip length
      intensity = map(level[i], 0, 40, 0, bandLength); //the amount of leds to be lit up, 50 is basically the max value any bin can have, 40 seems pretty responsive without maxing out a lot
    
      for (int j = ((i * bandLength) + (bandLength / 2)); j < ((i+1) * bandLength); j++) //start in the middle of each band length and go to the start of the next
      {
        if (j < ((i * bandLength) + (bandLength / 2) + (intensity / 2))) //for each LED in our 1/16 strip, check if we have lit less than the goal number/2 since its only the top half here
        {
          stripLEDs[j] = ColorFromPalette(currentPalette, i * (256 / channels), bright, LINEARBLEND); //set the color to be a fraction of the current palette, with the global brightness
        }
      }
      //now do the same in reverse
      for (int j = ((i * bandLength) + (bandLength / 2)); j > (i * bandLength); j--) //start in the middle of each band length and go backwards to the start of the band
      {
        if (j > ((i * bandLength) + (bandLength / 2) - (intensity / 2)))
        {
          //stripLEDs[j] = CHSV(i*(256 / channels), 220, bright);
          stripLEDs[j] = ColorFromPalette(currentPalette, i * (256 / channels), bright, LINEARBLEND);
        }
      }
    }
    FastLED.show();
    delay(20); //if we dont have enough delay it looks more flickery and annoying
  }
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
    //readFrequencies(i);
    //detectBumps(i);
    addBumpPixel(i);
  }
}

void linearSpectrumWithBumps()
{
  for (int i=0; i<7; i++)
  {
    //readFrequencies(i);
    //detectBumps(i);
    addBumpPixel(i);   
  }
  //addSingleColorPixel();
  delayMicroseconds(75);
}

void bumpsOnlyFlashesToNeutral()
{
  for (int i=0; i<7; i++)
  {
    //readFrequencies(i);
    //detectBumps(i);
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
    //readFrequencies(i);
    //detectBumps(i);
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
      //readFrequencies(i);
      //detectBumps(i);
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

void simpleChase(int bright) //need to document this one better, its kinda confusing and also doesnt work with big numbers.
{
  while (!updates) //just run this in a loop until theres updates, then it can return to the main loop function. However if we dont check for updates within the function we never exit
  {
    AIOClient.loop(); //check the data in the feeds while this is running so we know if we should stop ever. 
    int lit = 3;
    int space = 3;
    for (int j=0; j<space+lit; j++)
    {
      for (int i=0; i < (NUM_LEDS-1) - lit - space- j; i=i+lit+space)
      {
        stripLEDs(i+j, i+j+lit-1) = ColorFromPalette(currentPalette, baseHue, bright, LINEARBLEND); //
        stripLEDs(lit+j, j+lit+space-1) = CHSV(0, 0, 0);
      }
      FastLED.show();
      //delayMicroseconds(75);
      delay(55);
      FastLED.clear();
    }
    baseHue++; //move through the hues in the palette
  }
}

void bounce(int bright, int spd)
{
  while (!updates) //just run this in a loop until theres updates, then it can return to the main loop function. However if we dont check for updates within the function we never exit
  {
    AIOClient.loop(); //check the data in the feeds while this is running so we know if we should stop ever.
    EVERY_N_MILLIS_I(delayMs, 5)
    {
      delayMs.setPeriod(map(spd, 0, 100, 20, -1)); //dynamically update the speed based on the mqtt speed values. 20ms max delay is pretty slow, -1 with the map gives us 0ms
      fadeToBlackBy(stripLEDs, NUM_LEDS, 32); //1/8th fade every loop
      //stripLEDs[bounceLocation] = CHSV((triwave8(bounceLocation)), 255, bright);
      stripLEDs[bounceLocation] = ColorFromPalette(currentPalette, map(bounceLocation, 0, NUM_LEDS-1, 0, 255), bright, LINEARBLEND);
      FastLED.show();
      bounceLocation += bounceDirection;
      
      if (bounceLocation == 0)
      {
        bounceDirection = 1;
      }
      if (bounceLocation == NUM_LEDS-1)
      {
        bounceDirection = -1;
      }
    }
  } 
}

void bounceUsingWaves() //neat lightweight implementation using the triwave function and mapping it to our length. Issue is it wont hit every LED because waves only go 0-255
{
  for (uint8_t i=0; i<256; i++)
  {
    stripLEDs[map(triwave8(i), 0, 255, 0, NUM_LEDS-1)] = CHSV(sin8(i), 255, 150);
    FastLED.show();
    delayMicroseconds(150);
    FastLED.clear();
  }
}

//testing the sin8 and cos8 values which go from 0-255. Sin 64 =255. Sin 192 = 1. Period of 128? cos8(0) = 255, cos8(128) = 1, cos8(256) = 255.
//triwave is perfectly linear, period of 128 with values of 0-255. One increase in i = 2 increase in output. triwave(0) = 0
void testSin()
{
  int scale = 50;
  int x = random16();
  int y = random16();
  int z = random16();
  for (int i=0; i<NUM_LEDS; i++)
  {
    Serial.print("Current i value: ");
    Serial.print(i);
    Serial.print(". Triwave of i value: ");
    Serial.print(triwave8(i));
    Serial.print(". inoise8 of i value: ");
    Serial.println(inoise8((x + i*scale), (y + i*scale), z));
    x += scale / 4;
    y -= scale / 8;
    z += scale;
  }
  delay(9999999999999999999999999999999999999999999999999);
}

//set the entire strip to the same color from a palette. Then, select a random pixel and turn it to a different color, brighter, then fade it back to the "background" color
void twinkle(int bright, int spd)
{
  int i = 0;
  CRGB backgroundColor = ColorFromPalette(currentPalette, baseHue, bright, LINEARBLEND);
  while (!updates) //just run this in a loop until theres updates, then it can return to the main loop function. However if we dont check for updates within the function we never exit
  {
    AIOClient.loop(); //check the data in the feeds while this is running so we know if we should stop ever.
    EVERY_N_MILLIS_I(delayMs1, 50)
    {
      stripLEDs[pixelOrder[i]] = ColorFromPalette(currentPalette, random8(), 255, LINEARBLEND); //set a random pixel to random color, full brightness
      delayMs1.setPeriod(map(spd, 0, 100, 500, -1));
      FastLED.show();
      if (i == NUM_LEDS-1)
      {
        i = 0;
        arrayShuffle();
      }
      else
      {
        i++;
      }
    }
    EVERY_N_MILLIS_I(delayMs2, 5) //every two hundreth of a second fade the sparkles back to the background color
    {
      for (int i = 0; i < NUM_LEDS; i++)
      {
        stripLEDs[i] = nblend(stripLEDs[i], backgroundColor, 32); //blend by 1/8th back towards the background color
        FastLED.show();
      }
    }
    EVERY_N_MILLIS_I(delayMs3, 1000) //every second update the hue
    {
      baseHue += 5;
      backgroundColor = ColorFromPalette(currentPalette, baseHue, bright, LINEARBLEND);
    }
  }
}

void noise(int bright, int spd)
{
  while (!updates) //just run this in a loop until theres updates, then it can return to the main loop function. However if we dont check for updates within the function we never exit
  {
    AIOClient.loop(); //check the data in the feeds while this is running so we know if we should stop ever.
    EVERY_N_MILLIS_I(delayMs, 60)
    { 
      delayMs.setPeriod(map(spd, 0, 100, 200, -1));
      for (int i=0; i<NUM_LEDS; i++)
      {
        oldHueDiff[i] = currentHueDiff[i];
        currentHueDiff[i] = map(inoise8((randomX + i*scale), (randomY + baseHue*scale), randomZ), 0, 255, (-1 * (difference / 2)), (difference / 2)); //map the noise taken with 3 variables to a range of the difference, from -.5difference to +.5difference

        //next line is a very long one that just blends between the last noise and the current one for a color in the palette to smooth out the pixel flickering
        stripLEDs[i] = blend(ColorFromPalette(currentPalette, baseHue+currentHueDiff[i], bright/2, LINEARBLEND), ColorFromPalette(currentPalette, baseHue+oldHueDiff[i], bright/2, LINEARBLEND), 100);
        randomX += scale / 4; //update the x, y, and z values at different rates as we go down the strip so each light can be different
        randomY -= scale / 8;
        randomZ += scale;
      }
        FastLED.show();
        baseHue++; //move through the palette hues
    }
  }
}

//fisher-yates randomization for arrays. https://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
void arrayShuffle()
{
  //first we want to generate our array with NUM_LEDS elements in order before we swap
  for (int i = 0; i < NUM_LEDS; i++)
  {
    pixelOrder[i] = i;
  }
  //now we use Fisher-Yates to swap elements around
  for (int i = NUM_LEDS-1; i > 0; --i) //go from the max index down to 0
  {
    int j = random(0, i+1); //make a random number between 0 and i
    int t = pixelOrder[i]; //put the old value into a temporary storage variable
    pixelOrder[i] = pixelOrder[j]; //swap the max index with the random one
    pixelOrder[j] = t; //complete the swap
  }
}

//select a random color from the palette. Take an array of random pixel indices and set them one at a time to that color. Once all pixels are the new color, change the color and repeat
void dissolve(int bright, int spd)
{
  CRGB targetColor = ColorFromPalette(currentPalette, random8(), bright, LINEARBLEND);
  int i = 0;
  while (!updates) //just run this in a loop until theres updates, then it can return to the main loop function. However if we dont check for updates within the function we never exit
  {
    AIOClient.loop(); //check the data in the feeds while this is running so we know if we should stop ever.
    EVERY_N_MILLIS_I(delayMs, 60)
    { 
      delayMs.setPeriod(map(spd, 0, 100, 120, -1)); //update the delay so it is responsive.
      stripLEDs[pixelOrder[i]] = targetColor;
      FastLED.show();
      
      if (i == NUM_LEDS-1) //if we reach the end of the strip we want to go back to the start with a new color and a new order of pixels to light up
      {
        i = 0;
        CRGB oldColor = targetColor;
        targetColor = blend(oldColor,ColorFromPalette(currentPalette, random8(), bright, LINEARBLEND), 128); //get a new random color from the palette, blended with the old one 50%
        arrayShuffle();
      }
      else
      {
        i++;
      }
    }
  }
}

void handleFeeds(char* topic, byte* payload, unsigned int length) //the function that is called anytime there is new data in any of the feeds
{
  updates = true; //set the update flag to true because something has changed
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) 
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  
  if (strcmp(topic, "FillGee/feeds/effect") == 0) //we use this str compare function (which returns 0 when the strings are the same) to find out which topic it is
  {
    Serial.print("Updated the effect status to: ");
    payload[length] = '\0'; //add the escape character at the end of this byte we have a string in
    ctrl_effect = String((char*)payload); //cast the payload to a char and convert to a string which we store in our global effect variable
    Serial.println(ctrl_effect);
  }
  
  if (strcmp(topic, "FillGee/feeds/on-status") == 0) //this handles messages in the on-status feed
  {
    Serial.print("Updated the power status to: ");
    payload[length] = '\0';
    String s = String((char*)payload);
    if (s == "ON")
    {
      Serial.println("ON");
      powered = true;
    }
    if (s == "OFF")
    {
      Serial.println("OFF");
      powered = false;
    }
  }
  
  if (strcmp(topic, "FillGee/feeds/led-brightness") == 0) //handles the brightness feed which makes things bright or not
  {
    Serial.print("Updated the brightness to: ");
    payload[length] = '\0';
    String b = String((char*)payload);
    ctrl_brightness = b.toInt();
    Serial.println(ctrl_brightness);
  }

  if (strcmp(topic, "FillGee/feeds/speed") == 0) //handles the speed feed which makes faster or slower effects
  {
    Serial.print("Updated the speed to: ");
    payload[length] = '\0';
    String s = String((char*)payload);
    ctrl_speed = s.toInt();
    Serial.println(ctrl_speed);
  }
  
  if (strcmp(topic, "FillGee/feeds/threshold") == 0) //handles the audio threshold data
  {
    Serial.print("Updated audio threshold to: ");
    payload[length] = '\0';
    String s = String((char*)payload);
    ctrl_threshold = s.toInt();
    Serial.println(ctrl_threshold);
  }
  
  if (strcmp(topic, "FillGee/feeds/current-palette") == 0) //handles the palette switching
  {
    Serial.print("Updated the current palette to: ");
    payload[length] = '\0'; //add the escape character at the end of this byte we have a string in
    ctrl_palette = String((char*)payload); //cast the payload to a char and convert to a string which we store in our global palette variable
    Serial.println(ctrl_palette);
    //not sure if there is a better way of doing this since switch cases dont take strings, enums are annoying, etc. While it could be more elegant it might not be worth the effort
    if (ctrl_palette == "RainbowColors_p")
    {
      currentPalette = RainbowColors_p;
    }
    if (ctrl_palette == "RainbowStripeColors_p")
    {
      currentPalette = RainbowStripeColors_p;
    }
    if (ctrl_palette == "PartyColors_p")
    {
      currentPalette = PartyColors_p;
    }
    if (ctrl_palette == "OceanColors_p")
    {
      currentPalette = OceanColors_p;
    }
    if (ctrl_palette == "CloudColors_p")
    {
      currentPalette = CloudColors_p;
    }
    if (ctrl_palette == "ForestColors_p")
    {
      currentPalette = ForestColors_p;
    }
    if (ctrl_palette == "LavaColors_p")
    {
      currentPalette = LavaColors_p;
    }
    if (ctrl_palette == "HeatColors_p")
    {
      currentPalette = HeatColors_p;
    }
    
  }
}

void reconnect() //function that connects to AIO and subscribes to the feeds
{
  while (!AIOClient.connected())  // Loop until we're reconnected
  {
    Serial.print("Reconnecting to Adafruit IO... ");
    if (AIOClient.connect("teensyClient", AIO_USERNAME, AIO_KEY)) //try to connect to the AIO server with the username and password defined at the top
    {
      Serial.println("Connected successfully");
      AIOClient.subscribe(FEED1);
      AIOClient.subscribe(FEED2);
      AIOClient.subscribe(FEED3);
      AIOClient.subscribe(FEED4);
      AIOClient.subscribe(FEED5);
      AIOClient.subscribe(FEED6);
      AIOClient.subscribe(FEED7);
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(AIOClient.state());
      Serial.println(" try again in 5 seconds");
      delay(5000); // Wait 5 seconds before retrying
    }
  }
}

void doLights(String effect, String palette, int bright, int spd)
{
  updates = false;
  if (powered)
  {
    if (effect == "Simple Chase")
    {
      Serial.println("Starting chase function");
      simpleChase(bright); //implement speed later
    }  
    if (effect == "Bounce")
    {
      Serial.println("Starting bounce function");
      bounce(bright, spd);
    }
    if (effect == "Noise")
    {
      Serial.println("Starting noise function");
      noise(bright, spd);
    }
    if (effect == "Linear Frequency")
    {
      Serial.println("Starting the average frequency linear function");
      linearAverageFrequency(bright, spd);
    }
    if (effect == "EQ")
    {
      Serial.println("Starting the EQ function");
      EQ(bright);
    }
    if (effect == "EQMid")
    {
      Serial.println("Starting the EQ from the middle function");
      EQFromMid(bright);
    }
    if (effect == "Dissolve")
    {
      Serial.println("Starting the dissovle function");
      dissolve(bright, spd);
    }
    if (effect == "Twinkle")
    {
      Serial.println("Starting the twinkle function");
      twinkle(bright, spd);
    }    
    else
    {
      AIOClient.loop(); //check the data in the feeds
      //FastLED.clear();
      //FastLED.show();
      delay(25);
    }
  }
  else
  {
    AIOClient.loop(); //check the data in the feeds to see if we should turn back on
    FastLED.clear();
    FastLED.show();
    delay(25);
  }
}

void loop() 
{
  if (!AIOClient.connected()) //if we arent connected to the AIO feeds, reconnect
  {
    reconnect();
  }
  //AIOClient.loop(); //check the data in the feeds
  doLights(ctrl_effect, ctrl_palette, ctrl_brightness, ctrl_speed);
  //readAudioData();

  /***Pick ONLY ONE of the following effects to run *************/
  //noise(ctrl_brightness);
  //bumpsOnlyFlashesToNeutral();
  //bumpsOnlyFlashesToBlack();
  //rainbowGradientBumpStop();
  //linearSpectrumNoBumps();
  //linearSpectrumWithBumps();
  //bumpsOnlyLinear();
  //EQFromMid();
  //EQ();
  //addSingleColorPixel();
  //simpleChase();
  //bounceUsingWaves();
  //bounce(ctrl_brightness);
  //testSin();
}
