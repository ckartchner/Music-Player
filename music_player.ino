/***************************************************
  Designed for audio surprises. 

  Outline:
  1) Play interruptable file when shaken.
  2) Play file when hall effect sensor triggered (lid opened)
  3) After file finishes from lid opening, continue to play random files 
     at random intervals forever.
  
  Code written by: Carlin Kartchner

  Outline for player began with Adafruit VS1053 example code, player_simple
  Written by Limor Fried/Ladyada for Adafruit Industries.
  
  Thanks to Puck on StackOverflow for random file implementation:
  https://stackoverflow.com/questions/26002991/arduino-random-file-on-sd-rewinddirectory

  Sections user will want to consider editing:
  - User editable globals section
  - Files played. Obviously not everyone will have the same files. 
  - All code pertaining to the use of an LSM303 for shake detection as there are many ways to do this.

  Known issues:
  -Higher bitrate wav files will not play correctly with many microcontrollers due to a slower clock rate that cannot keep up. 
  Consider converting the wav to a compressed format for better performance. 

 ****************************************************/

// include SPI, MP3 and SD libraries
#include <SPI.h>
#include <Adafruit_VS1053.h>
#include <SD.h>
#include <Wire.h>
#include <LSM303.h>

// These are the pins used for the breakout
#define BREAKOUT_RESET  9      // VS1053 reset pin (output)
#define BREAKOUT_CS     10     // VS1053 chip select pin (output)
#define BREAKOUT_DCS    8      // VS1053 Data/command select pin (output)
#define CARDCS 4     // Card chip select pin
// DREQ should be an Int pin, see http://arduino.cc/en/Reference/attachInterrupt
#define DREQ 3       // VS1053 Data request, ideally an Interrupt pin

// Trigger pin setup
const int lidSwitch = 0;

// User editable globals
const int amag_trigger = 1.3; //This should be tested at final location before use. Sensitivity may vary between environments.  
const int vol = 30; //Volume. Lower number -> louder volume. 

// Files played. Should be edited by end user. 
const char trigger_audio[] = "Tron//17DISC~1.WMA"; //File played when switch is triggered. 
const char ready_audio[] = "PowUp.wma"; //Audio played when signaling the packge is ready to be triggered on opening. 
const char shake_audio[] = "meow~1.fla"; //Audio played when shaking is triggered
const char random_dir[] = "wavs/";  //Folder containing random file clips

// Initialize musicplayer object
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(BREAKOUT_RESET, BREAKOUT_CS, BREAKOUT_DCS, DREQ, CARDCS);

// Initialize accelerometer object
LSM303 compass; 

// Global variables. 
int lss = 0;      //lid switch state
int n_files = 0;  //number of files counted by countFiles
int rcnt = 0;     //Tracker for number of random counts played.
float amag = 0.0; //accelerometer magnitude

void setup() {
  Serial.begin(9600);
  Serial.println("Initializing music player");
  Wire.begin();

  //Initialize LSM303 for accelerometer (shaking) readings
  compass.init();
  compass.enableDefault();

  //generate a random seed so it doesn't do the same order every time!
  randomSeed(analogRead(0));

  if (! musicPlayer.begin()) { // initialise the music player
    Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
    while (1);
  }
  Serial.println(F("VS1053 found"));

  //SD.begin() needs to be called before doing other SD card communication!
  if (!SD.begin(CARDCS)) {
    Serial.println(F("SD failed, or not present"));
    while (1);  // don't do anything more
  }

  File folder = SD.open(random_dir);
  n_files = countFiles(folder);
  folder.close();
  
  //Unclear why, but if F() missing from Serial.print below, files won't play!
  //F() causes flash based memory strings to be used. 
  Serial.print(F("Number of files in random audio dir: ")); 
  Serial.println(n_files);
  
  // list all files on SD card
  //  printDirectory(SD.open("/"), 0);

  // Set volume for left, right channels. lower numbers == louder volume!
  // Remember, what sounds good during testing may not be audible in a loud environment.
  musicPlayer.setVolume(vol, vol);

  // If DREQ is on an interrupt pin (on uno, #2 or #3) we can do background
  // audio playing
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);  // DREQ int

  // Setup trigger switch
  pinMode(lidSwitch, INPUT);

  // Wait a min to get lid closed. Gives ample time to get setup before trigger is enabled. 
  Serial.println(F("Start of 1 min wait."));
  unsigned long startMillis = millis();
  while (millis() - startMillis < 60000);
  Serial.println(F("End of 1 min wait."));

  //play tone announcing the pkg is ready and g2g.
  musicPlayer.playFullFile(ready_audio);

}

void loop() {
  //Check if player is making sudden movements or shaking
  //default FS linear acceleration = +/- 2g
  //mg/LSB = 0.061
  compass.read();
  float ax = compass.a.x;
  float ay = compass.a.y;
  float az = compass.a.z;
  amag = sqrt(ax*ax + ay*ay + az*az); //magnitude of acceleration
  amag = amag*0.000061; //normalize (not calibrated, so doesn't equal 1)
  if (amag > 1.3) { 
    Serial.println(F("Shaking detected!"));
    if (musicPlayer.stopped()) {
      Serial.println(F("Trying to play shaking audio"));
      musicPlayer.startPlayingFile(shake_audio);
    }
  }
  
  //Check if box has been opened
  //Will interrupt shaking audio if currently playing.
  lss = digitalRead(lidSwitch);
  if (lss == 1){ //Pin goes HIGH when magnet is removed.
    Serial.println(F("Hall Effect switch detected!"));
    musicPlayer.stopPlaying();
    Serial.println(F("Music stopped!"));
    int status = musicPlayer.playingMusic;
    Serial.print(F("Is music playing:")); Serial.println(status);
    musicPlayer.softReset(); //allows for playing with interrupt!
    Serial.println(F("Soft reset preformed"));
    musicPlayer.playFullFile(trigger_audio);
    //After trigger music is finished, play random audio.
    randomAudio();
  }
  delay(500);
}

// Play a random audio file
void playRandomAudio(){
  int i, rand_song;  
  rcnt++; 
  File folder = SD.open(random_dir);
  File random_file;
  rand_song = random(0, n_files)+1;
  Serial.print(F("Random audio attempt: ")); Serial.println(rcnt);
  Serial.print(F("Random number: ")); Serial.println(rand_song);
  folder.rewindDirectory();  
  random_file = selectFileN(rand_song, folder);
  folder.close();
  Serial.print(F("Trying to play:")); Serial.println(random_file.name());
  //Uncertain if this is the best way to concatenate these strings, but it works!
  String lstr = String(random_dir) + String(random_file.name());
  Serial.print(F("Stated playing file:"));
  Serial.println(lstr);
  if(!musicPlayer.playFullFile(lstr.c_str())) {
    Serial.println(F("Could not open audio file"));
  }
  else Serial.println(F("Finished play attempt!"));
  random_file.close();

  Serial.println();
  Serial.println(); 
}

void randomAudio(){
  double dtime;

  //Unsure if this reset is still required, but it doesn't seem to cause any problems
  //Added when problems seen if the initial shaking audio was inturrupted by lid opening audio. 
  musicPlayer.softReset(); //allows for playing with interrupt!
  Serial.println(F("Soft reset preformed"));
    
  //Play initial random sound after 3 sec delay so audience knows to expect random audio.
  unsigned long startMillis = millis();
  while (millis() - startMillis < 3000);
  playRandomAudio();

  //Play audio file after random interval
  while(true) {
    //delay between 1 seconds and 5 minutes
    dtime = random(1000,300000);
    //delay between 1 and 5 seconds
    //dtime = random(1000, 5000);
    Serial.print(F("Delay time:")); Serial.println(dtime);
    unsigned long startMillis = millis();
    while (millis() - startMillis < dtime);
    playRandomAudio(); 
  }
  
}

// Select random file number
File selectFileN(int number, File dir) {
  int counter = 0;
  File return_entry;
  while(true) {
    File entry = dir.openNextFile();
    if (! entry) {
      Serial.println(F("Last file reached"));
      dir.rewindDirectory();
      break;
    }
    Serial.println(entry.name());
    counter++;
    if(counter==number) {
      return_entry = entry;
      dir.rewindDirectory();
      break;
    }
    entry.close();
  }
  return return_entry;
}

// Count the number of files in a given directory
int countFiles(File dir) {
  Serial.println(F("Counting files"));
  int counter = 0;
  while(true) {
    File entry = dir.openNextFile();
    if (! entry) {
      dir.rewindDirectory();
      break;
    }
    Serial.println(entry.name());
    counter++;
    entry.close();
  }
  Serial.println("----------------");
  return counter;
}

// File listing helper
// Included with many examples of the Arduino SD library
// Useful when adding new audio files. 
void printDirectory(File dir, int numTabs) {
  while (true) {
    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      //Serial.println("**nomorefiles**");
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      // files have sizes, directories do not
      Serial.print(F("\t\t"));
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}
