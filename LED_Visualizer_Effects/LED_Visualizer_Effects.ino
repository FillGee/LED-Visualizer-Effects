#include "FastLED.h"
#include "SPI.h"
#include "Ethernet.h"
#include "PubSubClient.h"

//no longer really needed with ADC, leaving so it compiles until rewrite made
#define STROBE 4
#define RESET 5
#define AUDIO_IN A2


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

//define all the FastLED strip info so its here to change if needed. Updated to use teensy 4.0 parallel output stuff
#define STRIP_DATA 14
#define NUM_STRIPS 1
#define NUM_LEDS_PER_STRIP 300
#define NUM_LEDS 300
#define STRIP_TYPE WS2812B
#define COLOR_ORDER GRB


CRGBArray<NUM_LEDS_PER_STRIP * NUM_STRIPS> stripLEDs; //make an array with an entry for each LED on the strip, of the type CRGB which is used by FastLED

/****************THIS WILL ALL BE DELETED NOW THAT THE MSGEQ7s ARE NO LONGER USED IN FAVOR OF ADC AUDIO PROCESSING ********************************/ 
int freq;
int Frequencies_One[7];
int audioAmplitudes[7];
int intensity;
int i;
int matrixRow;
int matrixColumn;


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
int baseHue;

void setup() 
{
  /* THIS WAS OLD MSGEQ7 AND MATRIX SETUP, NO LONGER NEEDED, KEEPING SO IT WILL COMPILE UNTIL RE-WRITE */
    pinMode(STROBE, OUTPUT);
    pinMode(RESET, OUTPUT);
    pinMode(AUDIO_IN, INPUT);  
    digitalWrite(STROBE, HIGH);
    digitalWrite(RESET, HIGH);
    Serial.begin(115200);
    
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

    FastLED.addLeds<NUM_STRIPS, WS2812B, STRIP_DATA, COLOR_ORDER>(stripLEDs, NUM_LEDS); //constructs the led strip with FastLED so it knows how to drive it
    FastLED.setBrightness(255); //set the brightness globally, everything else will be a percentage of this?
    FastLED.clear();
    FastLED.show();

    AIOClient.setServer(SERVER, PORT); //connect to the MQTT server and port defined above
    AIOClient.setCallback(handleFeeds); //set the function that will be called anytime that the MQTT client recieves data. In this case, the funciton handleFeeds()
    Ethernet.begin(mac); //start the internet connection with the MAC adress above. Add a comma and "ip" to not use DHCP
    delay(1500); //Allow the hardware to sort itself out

    FastLED.clear();
    FastLED.show();
    AIOClient.connect("teensyClient", AIO_USERNAME, AIO_KEY); //connect to the AIO feeds so we can write the default states to them so we dont have de-sync
    powered = true; //initially powered
    AIOClient.publish("FillGee/feeds/on-status", "ON"); //send the initial power state to MQTT so we dont have a de-sync on the dashboard
    ctrl_brightness = 125;
    AIOClient.publish("FillGee/feeds/led-brightness", "125"); //set initial brightness to 125 and send that over MQTT
    AIOClient.publish("FillGee/feeds/effect", "None"); //set initial brightness to 125 and send that over MQTT
    updates = false;
    Serial.println("Connected to Adafruit IO and set initial values!");
    AIOClient.disconnect(); //dont know why this is needed, but if we dont have it the MQTT updates never come through. I guess between publishing and subscribing it needs to disconnect?
    Serial.println("Disconnecting from Adafruit IO now that initial values have been set.");
}


/*************Pull frquencies from Spectrum Shield. No longer needed, will delete later****************/
void readFrequencies(int freq)
{
  //Read frequencies for each band
    audioAmplitudes[freq] = analogRead(AUDIO_IN);
    digitalWrite(STROBE, HIGH);
    delayMicroseconds(75);                    // Delay necessary due to timing diagram 
    digitalWrite(STROBE, LOW);
    delayMicroseconds  (100);                    // Delay necessary due to timing diagram 
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

//This will be changed significantly when new audio processing is handled. Goodbye all the hacky math
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

//This will be changed significantly when new audio processing is handled. Goodbye all the hacky math
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
    addBumpPixel(i);
  }
}

void linearSpectrumNoBumps()
{
  for (int i=0; i<7; i++)
  {
    readFrequencies(i);
  }
  addSingleColorPixel();
}

void linearSpectrumWithBumps()
{
  for (int i=0; i<7; i++)
  {
    readFrequencies(i);
    detectBumps(i);
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

void simpleChase(int ctrl_brightness) //need to document this one better, its kinda confusing and also doesnt work with big numbers.
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
        stripLEDs(i+j, i+j+lit-1) = CHSV(triwave8(i+j), 255, ctrl_brightness);
        stripLEDs(lit+j, j+lit+space-1) = CHSV(0, 0, 0);
      }
      FastLED.show();
      //delayMicroseconds(75);
      delay(55);
      FastLED.clear();
    }
  }
}

void bounce(int ctrl_brightness)
{
  uint8_t spd = 1;
  for (uint16_t i=0; i<NUM_LEDS-spd; i+=spd)
  {
    fadeToBlackBy(stripLEDs, NUM_LEDS, (64 /  spd));
    stripLEDs[i] = CHSV((triwave8(i)), random8(150,255), ctrl_brightness);
    FastLED.show();
    //FastLED.clear();
    delayMicroseconds(75);
  }
  for (uint16_t j=NUM_LEDS-1; j>=0+spd; j-=spd)
  {
    fadeToBlackBy(stripLEDs, NUM_LEDS, (64 / spd));
    stripLEDs[j] = CHSV((triwave8(j)), random8(150,255), ctrl_brightness);
    FastLED.show();
    //FastLED.clear();
    delayMicroseconds(75);
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

void lighthouse()
{
  
}

void noise(int bright)
{
  int scale = 100; //makes the noise more... noisy
  int difference = 85; //the maximum difference in hues that the "noise" can have from the base hue
  int x = random16(); //get random numbers for x, y, z to seed the noise
  int y = random16();
  int z = random16();
  while (!updates) //just run this in a loop until theres updates, then it can return to the main loop function. However if we dont check for updates within the function we never exit
  {
    AIOClient.loop(); //check the data in the feeds while this is running so we know if we should stop ever.  
    for (int i=0; i<NUM_LEDS; i++)
    {
      oldHueDiff[i] = currentHueDiff[i];
      currentHueDiff[i] = map(inoise8((x + i*scale), (y + baseHue*scale), z), 0, 255, (-1 * (difference / 2)), (difference / 2)); //map the noise taken with 3 variables to a range of the difference, from -.5difference to +.5difference
    
      CHSV blendedColor = blend(CHSV(baseHue+oldHueDiff[i], 255, bright), CHSV(baseHue+currentHueDiff[i], 255, bright), 100); //blend between color + old noise and new noise to have a smoother looking transition that can still update quickly
      stripLEDs[i] = blendedColor;
      x += scale / 4; //update the x, y, and z values at different rates as we go down the strip so each light can be different
      y -= scale / 8;
      z += scale;
    }
  FastLED.show();
  delay(60);
  baseHue = ((baseHue + 1) % 255); //change the baseHue slowly each loop and make sure it doesnt go over 255.
  }
}

void handleFeeds(char* topic, byte* payload, unsigned int length) //the function that is called anytime there is new data in any of the feeds
{
  updates = true; //set the update flag to true because something has changed
  //right now just display the message, will write the switch cases later
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) {
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
  
  if (strcmp(topic, "FillGee/feeds/current-palette") == 0) //handles the palette switching
  {
    Serial.print("Updated the current palette to: ");
    payload[length] = '\0'; //add the escape character at the end of this byte we have a string in
    ctrl_palette = String((char*)payload); //cast the payload to a char and convert to a string which we store in our global palette variable
    Serial.println(ctrl_palette);
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
      simpleChase(bright); //implement speed, palette later
    }
    if (effect == "Bounce")
    {
      Serial.println("Starting bounce function");
      bounce(bright); //implement speed, palette later
    }
    if (effect == "Noise")
    {
      Serial.println("Starting noise function");
      noise(bright);
    }
    else
    {
      FastLED.clear();
      FastLED.show();
      delay(25);
    }
  }
  else
  {
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
  AIOClient.loop(); //check the data in the feeds
  doLights(ctrl_effect, ctrl_palette, ctrl_brightness, ctrl_speed);

  /***Pick ONLY ONE of the following effects to run *************/
  //noise();
  //bumpsOnlyFlashesToNeutral();
  //bumpsOnlyFlashesToBlack();
  //rainbowGradientBumpStop();
  //linearSpectrumNoBumps();
  //linearSpectrumWithBumps();
  //bumpsOnlyLinear();
  //sevenBandEQFromMid();
  //sevenBandEQ();
  //simpleChase();
  //bounceUsingWaves();
  //bounce();
  //testSin();
}
