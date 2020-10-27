/*
   aGotino a simple Goto with Arduino (Nano/Uno)

      * move Right Ascension at 1x 
      * at button1&2 press, cycle among forward and backward speeds on RA&Dec
      * listen on serial port for basic LX200 commands (INDI LX200 Basic)
      * listen on serial port for aGotino commands:    
          xHHMMSSdDDMMSS set/go to position (d is Dec sign + or -)
          xMn            set/go to Messier object n
          xSn            set/go to Star number n in aGotino Star List
              x can be s (set) or g (goto)
          rRRRRdDDDD     slew by Ra&Dec by RRRR&DDDD degree mins
                           (RRRR*4 corresponds to seconds hours)
                           r & d are signs  (r = + is clockwise)
          +/-debug       verbose output
          +/-sleep       disable power saving (default enabled, DEC is set to sleep)
          +/-speed       increase or decrease speed by 4x at button press (default 8x)
          +/-range       increase or decrease max slew range by 15°(default 30°)
          
        WARNING: watch your scope while slewing! 
                 There are no controls to avoid collisions with mount

    This code or newer versions at https://github.com/mappite/aGotino
    
    by gspeed @ astronomia.com / qde / cloudynights.com forum
    This code is free software under GPL v3 License use at your risk and fun ;)

 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *

   How to calculate STEP_DELAY to drive motor at right sidereal speed for your mount

   Worm Ratio                144   // 144 eq5/exos2, 135 heq5, 130 eq3-2
   Other (Pulley/Gear) Ratio   2.5 // depends on your pulley setup e.g. 40T/16T = 2.5
   Steps per revolution      400   // or usually 200 depends on your motor
   Microstep                  32   // depends on driver

   MICROSTEPS_PER_DEGREE   12800   // = WormRatio*OtherRatio*StepsPerRevolution*Microsteps/360
                                   // = number of microsteps to rotate the scope by 1 degree

   STEP_DELAY              18699   // = (86164/360)/(MicroSteps per Degree)*1000000
                                   // = microseconds to advance a microstep at 1x
                                   // 86164 is the number of secs for earth 360deg rotation (23h56m04s)

 * Update the values below to match your mount/gear ratios and default values: * * * * * * */

const unsigned long MICROSTEPS_PER_DEGREE = 12800; // see above calculation
const unsigned long STEP_DELAY = 18699; // RA 1x microstep timing in micros, see above
const unsigned long MICROSTEPS = 32;    // microstep per step, depends on driver

const long SERIAL_SPEED = 9600;         // serial interface baud. Make sure your computer or phone match this
long MAX_RANGE = 1800;                  // max allowed movement range in deg minutes (1800'=30°).

boolean POWER_SAVING_ENABLED = true;    // toggle with -sleep on serial, see decSleep()
boolean DEBUG = false;                  // toggle with +debug on serial

/*
 * It is safe to keep the below untouched
 * 
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <catalogs.h> // load messier objects and others

unsigned int RA_FAST_SPEED  = 8;  // speed at button press, times the sidereal speed
unsigned int DEC_FAST_SPEED = 8;  // speed at button press
unsigned long STEP_DELAY_SLEW = 1000;   // step timing in micros when slewing (slewing speed)
// This assumes same gear ratios in AR&DEC
const unsigned long MICROSTEPS_PER_HOUR  = MICROSTEPS_PER_DEGREE * 360 / 24;

// pin connections
const int raDirPin  = 4;
const int raStepPin = 3;
const int raButtonPin = 6; // note this was 2 in ARto.ino
const int raEnableMicroStepsPin  = 2;
const int decDirPin  = 12;
const int decStepPin = 11;
const int decButtonPin =  7;
const int decEnableMicroStepsPin = 9;
const int decSleepPin = 10;

// STEP_DELAY_MIN:
// RA pulse buffer time in micros: when at STEP_DELAY_MIN micros from having to change
// pulse status, the (blocking) delayMicroseconds is called to allow  a perfect smooth
// pulse signal. Value must be less than 16383  (arduino limit for  delayMicroseconds)
// but more than what logic tests do at each loop() to honor STEP_DELAY
unsigned long STEP_DELAY_MIN = 800; // 1/4 of this is used fro Dec move while RA is playing

unsigned long raLastTime;         // last time RA  pulse has changed status
unsigned long raPressTime = 0;    // time when RA button is pressed
unsigned long decLastTime = 0;    // last time DEC pulse has changed status
unsigned long decPressTime = 0;   // time when DEC button is pressed
boolean raStepPinStatus = false;  // true = HIGH, false = LOW
boolean decStepPinStatus = false; // true = HIGH, false = LOW

// default current DEC to North
const long NORTH_DEC= 324000; // 90°

// Current and input coords in Secs
long currRA = 0;     
long currDEC = NORTH_DEC;
long inRA  = 0;
long inDEC = 0;
const long MIN_RA = 0;
const long MAX_RA = 24*3600;
const long MIN_DEC = -90*3600;
const long MAX_DEC = 90*36000;

int raSpeed  = 1;    // default RA speed (start at 1x to follow stars)
int decSpeed = 0;    // default DEC speed (don't move)
unsigned long i = 0; // for debug&loops

char input[20];     // stores serial input
int  in = 0;        // current char in serial input

String lx200RA = "00:00:00#"; // stores current RA in lx200 format 
String lx200DEC= "+90*00:00#"; // stores current DEC in lx200 format


void setup() {
  Serial.begin(SERIAL_SPEED);
  Serial.print("aGotino:");
  // init Arduino->Driver Motors pins as Outputs
  pinMode(raStepPin, OUTPUT);
  pinMode(raDirPin,  OUTPUT);
  pinMode(raEnableMicroStepsPin, OUTPUT);
  pinMode(decStepPin, OUTPUT);
  pinMode(decDirPin,  OUTPUT);
  pinMode(decEnableMicroStepsPin, OUTPUT);
  pinMode(decSleepPin, OUTPUT);
  // set Dec asleep:
  decSleep(true);
  // set AR motor direction clockwise (HIGH)
  digitalWrite(raDirPin, HIGH);
  // make sure no pulse signal is sent
  digitalWrite(raStepPin,  LOW);
  digitalWrite(decStepPin, LOW);
  // enable microstepping on both motors
  digitalWrite(raEnableMicroStepsPin, HIGH);
  digitalWrite(decEnableMicroStepsPin, HIGH);
  // init Button pins as Input Pullup so no need resistor
  pinMode(raButtonPin,  INPUT_PULLUP);
  pinMode(decButtonPin, INPUT_PULLUP);
  // init led and turn on for 0.5 sec
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println(" ready.");
  raLastTime = micros(); // Resolution is 4microsecs on 16Mhz board
  lx200DEC[3] = char(223); // set correct char in string...
}

/* RA Play */
void raPlay(unsigned long stepDelay) {
  unsigned long halfStepDelay = stepDelay/2; // duration of each pulse
  unsigned long dt  = micros() - raLastTime;  // delta time elapsed since previus stepPin status change
  // micros() overflow after approximately 70 minutes but "-" logic above will work anyway to calculate dt

  if ( (halfStepDelay - dt) < STEP_DELAY_MIN) { // need to switch pulse in halfStepDelay-dt micros, let's do it:
    // remaining delay to keep current pin status to honor halfStepDelay
    delayMicroseconds(halfStepDelay - dt); // which is less than STEP_DELAY_MIN
    raStepPinStatus = !raStepPinStatus;
    digitalWrite(raStepPin, (raStepPinStatus ? HIGH : LOW)); //change pin status
    raLastTime = micros(); // last time we updated the pin status
  } else if ( dt > halfStepDelay)  { // no luck, too fast, we can't honor halfStepDelay.
    Serial.print("aGotino: RA can't honor the speed, reset, dt was: ");
    Serial.println(dt);
    raLastTime = micros(); // reset time
  }
}

/* DEC move */
void decPlay(unsigned long stepDelay) {
  // Same logic as arPlay but with  STEP_DELAY_MIN/4 to make sure RA movement is not impacted
  unsigned long halfStepDelay = stepDelay/2;
  unsigned long dt  = micros() - decLastTime;

  if ( (halfStepDelay - dt) < (STEP_DELAY_MIN / 4) ) { // time to hold and change pulse
    delayMicroseconds(halfStepDelay - dt);
    decStepPinStatus = !decStepPinStatus;
    digitalWrite(decStepPin, (decStepPinStatus ? HIGH : LOW));
    decLastTime = micros(); // reset time
  } else if ( dt > halfStepDelay ) { // too late!
    // reset time but don't output any error message in serial
    decLastTime = micros();
  }
  /** debug **
    i++;
    if (i == 1234) {
      Serial.print("decLastTime: ");Serial.println(decLastTime);
      Serial.print("halfStepDelay: "); Serial.println(halfStepDelay);
      Serial.print("dt: "); Serial.println(dt);
      Serial.print("delayMicroseconds:"); Serial.println((halfStepDelay-dt));
      delay(5000);
      i= 0;
    }
    /* *** */
}


/*
 *  Slew RA and Dec by seconds (Hours and Degree secs)
 *   motors direction is set according to sign
 *   RA 1x direction is reset at the end
 *   microstepping is disabled for fast movements and (re-)enabled for finer ones
 */
int slewRaDecBySecs(long raSecs, long decSecs) {

  // check if within max range
  if ( (abs(decSecs) > (MAX_RANGE*60)) || ( abs(raSecs) > (MAX_RANGE*4)) ) {
    return 0; // failure
  }
  
  // set directions
  digitalWrite(raDirPin,  (raSecs > 0 ? HIGH : LOW));
  digitalWrite(decDirPin, (decSecs > 0 ? HIGH : LOW));

  // FIXME: detect if direction has changed and add back-slash steps

  // calculate how many (micro)steps are needed
  unsigned long raSteps  = (abs(raSecs) * MICROSTEPS_PER_HOUR) / 3600; // FIXME: implicit conversion from long to unsigned long, will it work?
  unsigned long decSteps = (abs(decSecs) * MICROSTEPS_PER_DEGREE) / 3600;

  unsigned long raFullSteps   = raSteps / MICROSTEPS;             // this will truncate the result...
  unsigned long raMicroSteps  = raSteps - raFullSteps * MICROSTEPS; // ...remaining microsteps
  unsigned long decFullSteps  = decSteps / MICROSTEPS;            // this will truncate the result...
  unsigned long decMicroSteps = decSteps - decFullSteps * MICROSTEPS; // ...remaining microsteps

  // Fast Move - disable microstepping (enable full steps)
  printLog("Disabling Microstepping");
  digitalWrite(raEnableMicroStepsPin, LOW);
  digitalWrite(decEnableMicroStepsPin, LOW);
  printLog(" RA FullSteps:  ");
  printLogUL(raFullSteps);
  printLog(" DEC FullSteps: ");
  printLogUL(decFullSteps);
  slewRaDecBySteps(raFullSteps, decFullSteps);
  printLog("FullSteps Slew Done");

  // Final Adjustment - re-enable micro stepping
  // FIXME: this is likely superflous since precision is 6.66 full steps per minute,
  //        i.e. one full step is already less than a minute... 
  printLog("Re-enabling Microstepping");
  digitalWrite(raEnableMicroStepsPin, HIGH);
  digitalWrite(decEnableMicroStepsPin, HIGH);
  printLog(" RA MicroSteps:");
  printLogUL(raMicroSteps);
  printLog(" DEC MicroSteps:");
  printLogUL(decMicroSteps);
  slewRaDecBySteps(raMicroSteps, decMicroSteps);
  printLog("MicroSteps Slew Done");

  // reset RA to right (1x) direction
  digitalWrite(raDirPin,  HIGH);
  // reset RA time 
  raLastTime = micros();
  return 1;
}

/*
 *  Slew RA and Dec by steps
 *   . assume direction and microstepping is set
 *   . listen on serial port and reply to lx200 GR&GD
 *     commands with current (initial) position to avoid
 *     INDI timeouts during long slewings actions
 */
void slewRaDecBySteps(unsigned long raSteps, unsigned long decSteps) {
  digitalWrite(LED_BUILTIN, HIGH);

  // wake up Dec motor if needed
  if (decSteps != 0) {
    decSleep(false);
  }

  // FIXME: implement better accelleration
  // when step i is
  //    i < 100 (first 100)
  //    i > raStep-100  && i < raStep
  //    i > decStep-100 && i < decStep
  // move both motors at half speed
  unsigned long delaySlew = 0; 
  unsigned long delayLX200Micros = 0; // delay introduced by LX200 polling reply
  in = 0; // reset the  input buffer read indec
  
  for (unsigned long i = 0; (i < raSteps || i < decSteps) ; i++) {
    if ((i<100)||(i>raSteps-100 && i<raSteps)|| (i>decSteps-100 && i<decSteps)) {
      delaySlew = STEP_DELAY_SLEW*2;// twice as slow
    } else { 
      delaySlew = STEP_DELAY_SLEW;  // full speed
    } 
    
    if (i < raSteps)  { digitalWrite(raStepPin,  HIGH); }
    if (i < decSteps) { digitalWrite(decStepPin, HIGH); }
    delayMicroseconds(delaySlew);
    
    if (i < raSteps)  { digitalWrite(raStepPin,  LOW);  }
    if (i < decSteps) { digitalWrite(decStepPin, LOW);  }
    
    // support LX200 polling while slewing
    delayLX200Micros = 0;
    if (Serial.available() > 0) {
      delayLX200Micros = micros();
      input[in] = Serial.read();
      if (input[in] == '#' ) {
        if (in>1 && input[in-1] == 'R') { // :GR#
          Serial.print(lx200RA);
        } else if (in>2 && input[in-1] == 'D') { // :GD#
          Serial.print(lx200DEC);
        }
        in = 0;
      } else {
        if (in++ >5) in = 0;
      }
      delayLX200Micros = micros()-delayLX200Micros;
      if (delayLX200Micros>delaySlew) Serial.println("LX200 polling slows too much!");
    } 
    delayMicroseconds(delaySlew-delayLX200Micros);
  }
  // set Dec to sleep
  if (decSteps != 0) {
    decSleep(true);
  }
  digitalWrite(LED_BUILTIN, LOW);
}

/*
 * Put DEC motor Driver to sleep to save power or wake it up
 *
   FIXME: issue with glitces at wake up with DRV8825
          motor resets to home position (worst case 4 full steps, i.e. less than a minute)
    https://forum.pololu.com/t/sleep-reset-problem-on-drv8825-stepper-controller/7345
    https://forum.arduino.cc/index.php?topic=669304.0
 */
void decSleep(boolean b) {
  if (POWER_SAVING_ENABLED) {
    if (b == false) { // wake up!
      digitalWrite(decSleepPin, HIGH);
      // FIXME:for ST4 Support this need to be changed to avoid RA delays
      delayMicroseconds(2000); // as per DRV8825 specs drivers needs up to 1.7ms to wake up and stabilize
    } else {
      digitalWrite(decSleepPin, LOW); // HIGH = active (high power) LOW = sleep (low power consumption)
    }
  } else {
    digitalWrite(decSleepPin, HIGH);
  }
}

/* 
 *  Basic Meade LX200 Protocol support:
 *    reply to #:GR# and #:GD# with current RA (currRA) and DEC (currDEC) position
 *    get #:Q#:Sr... to set input RA (inRA) and :Sd... to set input DEC (inDEC)
 *    if currDEC is NORTH, 
 *       assume this is the very first set operation and set current DEC&RA to input DEC&RA
 *    else 
 *       calculate deltas and slew to inRA and inDEC
 *     
 *  An lx200 session with Stellarium is (> = from Stellarium to Arduino)
> #:GR#
< 02:31:49#
> #:GD#
< +89ß15:51#
> #:Q#:Sr 11:22:33#
< 1
> :Sd +66*55:44#
< 1
> :MS#
> #:GR#
< 05:04:21#
> #:GD#
< +81ß11:39#
*/
void lx200(String s) { // all :.*# commands are passed 
  if (s.substring(1,3).equals("GR")) { // :GR# 
    printLog("GR");
    // send current RA to computer
    Serial.print(lx200RA);
  } else if (s.substring(1,3).equals("GD")) { // :GD# 
    printLog("GD");
    // send current DEC to computer
    Serial.print(lx200DEC);
  } else if (s.substring(1,3).equals("Sr")) { // :Sr HH:MM:SS# 
    printLog("Sr");
    // this is INITAL step for setting position (RA)
    long hh = s.substring(4,6).toInt();
    long mi = s.substring(7,9).toInt();
    long ss = s.substring(10,12).toInt();
    inRA = hh*3600+mi*60+ss;
    Serial.print(1);
  } else if (s.substring(1,3).equals("Sd")) { // :Sd sDD*MM:SS# 
    printLog("Sd");
    // this is the FINAL step of setting a pos (DEC) 
    long dd = s.substring(5,7).toInt()*(s.charAt(4)=='-'?-1:1);
    long mi = s.substring(8,10).toInt();
    long ss = s.substring(11,13).toInt();
    inDEC = dd*3600+mi*60+ss;
    if (currDEC == NORTH_DEC) { // if currDEC is still the initial default position (North)
      // assume this is to sync current position to new input
      currRA  = inRA;
      currDEC = inDEC;
      updateLx200Coords(currRA, currDEC); // recompute strings
    } 
    Serial.print(1);
  } else if (s.substring(1,3).equals("MS")) { // :MS# move
    printLog("MS");
    // assumes Sr and Sd have been processed
    // inRA and inDEC have been set, now it's time to move
    long deltaRaSecs  = currRA-inRA;
    long deltaDecSecs = currDEC-inDEC;
    // FIXME: need to implement checks, can't wait for slewRaDecBySecs
    //        reply since it may takes several seconds:
    Serial.print(0); // slew is possible 
    // slewRaDecBySecs reply to lx200 polling with current position until slew ends:
    if (slewRaDecBySecs(deltaRaSecs, deltaDecSecs) == 1) { // success         
      currRA  = inRA;
      currDEC = inDEC;
      updateLx200Coords(currRA, currDEC); // recompute strings
    } else { // failure
      Serial.print("1Range_too_big#");
    }
  } else if (s.substring(1,3).equals("CM")) { // :CM# sync
    // assumes Sr and Sd have been processed
    // sync current position with input
    printLog("CM");
    currRA  = inRA;
    currDEC = inDEC;
    Serial.print("Synced#");
    updateLx200Coords(currRA, currDEC); // recompute strings
  }
}

/* Update lx200 RA&DEC string coords so polling 
 * (:GR# and :GD#) gets processed faster
 */
void updateLx200Coords(long raSecs, long decSecs) {
  unsigned long pp = raSecs/3600;
  unsigned long mi = (raSecs-pp*3600)/60;
  unsigned long ss = (raSecs-mi*60-pp*3600);
  lx200RA = "";
  if (pp<10) lx200RA.concat('0');
  lx200RA.concat(pp);lx200RA.concat(':');
  if (mi<10) lx200RA.concat('0');
  lx200RA.concat(mi);lx200RA.concat(':');
  if (ss<10) lx200RA.concat('0');
  lx200RA.concat(ss);lx200RA.concat('#');

  pp = abs(decSecs)/3600;
  mi = (abs(decSecs)-pp*3600)/60;
  ss = (abs(decSecs)-mi*60-pp*3600);
  lx200DEC = "";
  lx200DEC.concat(decSecs>0?'+':'-');
  if (pp<10) lx200DEC.concat('0');
  lx200DEC.concat(pp);lx200DEC.concat(char(223));
  if (mi<10) lx200DEC.concat('0'); 
  lx200DEC.concat(mi);lx200DEC.concat(':');
  if (ss<10) lx200DEC.concat('0');
  lx200DEC.concat(ss);lx200DEC.concat('#');
 } 

/*
 * aGoto simple protocol
 */
void agoto(String s) {
  // remove blanks
  s.replace(" ", "");
  // keywords: debug, sleep, range, speed  
  if (s.substring(1,6).equals("debug")) {
    DEBUG = (s.charAt(0) == '+')?true:false;
    if (DEBUG) Serial.println("Debug On"); 
          else Serial.println("Debug Off"); 
  } else  if (s.substring(1,6).equals("sleep")) {
    POWER_SAVING_ENABLED = (s.charAt(0) == '+')?true:false;
    if (POWER_SAVING_ENABLED) Serial.println("Power Saving Enabled");
                         else Serial.println("Power Saving Disabled");
  } else  if (s.substring(1,6).equals("range")) {
    int d = (s.charAt(0) == '+')?15:-15;
    if (MAX_RANGE+d > 0 ) {
      MAX_RANGE = MAX_RANGE+d*60;
      Serial.println("New max range set to degrees:");
      Serial.println(MAX_RANGE);
    } else {
      Serial.println("Can't set range to zero");
    }
  } else  if (s.substring(1,6).equals("speed")) {
    int d = (s.charAt(0) == '+')?4:-4;
    if (RA_FAST_SPEED+d > 0 ) {
      RA_FAST_SPEED  = RA_FAST_SPEED+d;
      DEC_FAST_SPEED = DEC_FAST_SPEED+d;
      Serial.println("New fast RA&Dec speed set to");
      Serial.println(RA_FAST_SPEED);
    } else {
      Serial.println("Can't set speed to zero");
    }
  } else { // Move, Set or Goto commands

    long deltaRaSecs  = 0; // secs to move RA 
    long deltaDecSecs = 0; // secs to move Dec

    if (s.charAt(5) == '+' || s.charAt(5) == '-') { // rRRRRdDDDD (r and d are signs) - Move by rRRRR and dDDDD deg mins
      // toInt() returns 0 if conversion fails, logic belows detects this
      if (!s.substring(1, 5).equals("0000")) {
        deltaRaSecs = s.substring(1, 5).toInt() * (s.charAt(0) == '+' ? -1 : +1) * 4;// sign reversed to honor result (E > 0)
        if (deltaRaSecs == 0) { Serial.println("RA conversion error"); return; }
      }
      if (!s.substring(6, 10).equals("0000")) {
        deltaDecSecs = s.substring(6, 10).toInt() * (s.charAt(5) == '+' ? -1 : +1) * 60; // sign reversed to honor result (N > 0)
        if (deltaDecSecs == 0) { Serial.println("Dec conversion error"); return; }
      }
      long tmp_inRA = currRA - deltaRaSecs;
      long tmp_inDEC = currDEC - deltaDecSecs;
      if ( (tmp_inRA<MIN_RA   || tmp_inRA>MAX_RA) || 
           (tmp_inDEC<MIN_DEC || tmp_inDEC>MAX_DEC) ) {
        Serial.println("Values out of range"); return; 
      } else {
        inRA = tmp_inRA ;
        inDEC = tmp_inDEC;      
      }
    } else { // decode coords to inRA and inDEC
      if (s.charAt(1) == 'M' || s.charAt(1) == 'm') { // MESSIER coords
        int m = s.substring(2,5).toInt(); // toInt() returns 0 if conversion fails
        if (m == 0 || m > 110) { Serial.println("Messier number conversion error"); return; } 
        // this would fail from progmem: inRA =  Messier[m].ra;
        inRA = (long) pgm_read_dword(&(Messier[m].ra));
        inDEC= (long) pgm_read_dword(&(Messier[m].dec));
        Serial.print(s[0]=='s'?"Set ":"Goto ");
        Serial.print("M");Serial.println(m);
      } else if (s.charAt(1) == 'S' || s.charAt(1) == 's') { // STARS coords
        int n = s.substring(2,5).toInt(); // toInt() returns 0 if conversion fails
        if (n < 0 || n > 244) { Serial.println("Star number conversion error"); return; } 
        inRA = (long) pgm_read_dword(&(Stars[n].ra));
        inDEC= (long) pgm_read_dword(&(Stars[n].dec));
        Serial.print(s[0]=='s'?"Set ":"Goto ");
        Serial.print("Star ");Serial.println(n);
      } else { // HHMMSSdDDMMSS coords
        inRA  = s.substring(1, 3).toInt() * 60 * 60 + s.substring(3, 5).toInt() * 60 + s.substring(5, 7).toInt();
        inDEC = (s.charAt(7) == '+' ? 1 : -1) * (s.substring(8, 10).toInt() * 60 * 60 + s.substring(10, 12).toInt() * 60 + s.substring(12, 14).toInt());
        if (inRA == 0 || inDEC == 0) { Serial.println("Coordinates conversion error"); return; }
      }
      
      // inRA&inDEC are set
      if (s.charAt(0) == 's') { // set
          currRA  = inRA;
          currDEC = inDEC;
          Serial.print("Current Position: ");
          printCoord(currRA, currDEC);
          printLog("RA/DEC in secs:");
          printLogL(currRA);
          printLogL(currDEC);
        } else { // goto
          deltaRaSecs  = currRA-inRA;
          deltaDecSecs = currDEC-inDEC;
        }
      }

      // deltaRaSecs and deltaDecSecs are not zero if slew is needed
      if (deltaRaSecs != 0 || deltaDecSecs != 0) { 
        printLog("delta RA/DEC in secs:");
        printLogL(deltaRaSecs);
        printLogL(deltaDecSecs);
        /* SLEW! */
        Serial.println(" *** moving...");
        if ( slewRaDecBySecs(deltaRaSecs, deltaDecSecs) == 1) { // success
          Serial.println(" *** done");
          // set new current position
          currRA  = inRA;
          currDEC = inDEC;
          Serial.print("Current Position: ");
          printCoord(currRA, currDEC);
        } else{ 
          Serial.println("Values exceed max allowed range of degrees:");
          Serial.println(MAX_RANGE/60);
        }
      }
  }
}

// Print nicely formatted coords
void printCoord(long raSecs, long decSecs) {
  long pp = raSecs/3600;
  Serial.print(pp);
  Serial.print("h");
  long mi = (raSecs-pp*3600)/60;
  if (mi<10) = Serial.print('0');
  Serial.print(mi);
  Serial.print("'");
  long ss = (raSecs-mi*60-pp*3600);
  if (ss<10) = Serial.print('0');
  Serial.print(ss);
  Serial.print("\" ");
  pp = abs(decSecs)/3600;
  Serial.print((decSecs>0?pp:-pp));
  Serial.print("°");
  mi = (abs(decSecs)-pp*3600)/60;
  if (mi<10) = Serial.print('0');
  Serial.print(mi);
  Serial.print("'");
  ss = (abs(decSecs)-mi*60-pp*3600);
  if (ss<10) = Serial.print('0');
  Serial.print(ss);
  Serial.println("\"");
}

void loop() {
  // Move RA 
  raPlay(STEP_DELAY / raSpeed);
  
  // logic executed at each loop must take less than (STEP_DELAY/2-STEP_DELAY_MIN)
  // and less than STEP_DELAY_MIN micros to honor STEP_DELAY for perfect AR 1x speed

  // Move Dec if needed, not if ra is moving fast 
  // FIXME: test what happens if speed is not 1, it may work as well
  if (raSpeed == 1 && decSpeed != 0) {
    decPlay(STEP_DELAY / decSpeed);
  }

  // raButton pressed: skip if within 500ms from last press
  if ( (digitalRead(raButtonPin) == LOW)  && (micros() - raPressTime) > (500000) ) {
    raPressTime = micros();
    printLog("RA Speed: ");
    // 1x -> +RA_FAST_SPEED -> -(RA_FAST_SPEED-2)
    if (raSpeed == 1) {
      raSpeed = RA_FAST_SPEED;
      digitalWrite(raDirPin, HIGH);
    } else if  (raSpeed == RA_FAST_SPEED) {
      raSpeed = (RA_FAST_SPEED - 2);
      digitalWrite(raDirPin, LOW); // change direction
    } else if  (raSpeed == (RA_FAST_SPEED - 2)) {
      raSpeed = 1;
      digitalWrite(raDirPin, HIGH);
    }
    printLogUL(raSpeed);
    raLastTime = micros(); // reset time
  }
  
  // decButton pressed: skip if within 500ms from last press
  if (digitalRead(decButtonPin) == LOW && (micros() - decPressTime) > (500000) ) {
    decPressTime = micros();  // time when button has been pressed
    printLog("Dec Speed: ");
    // 0x -> +DEC_FAST_SPEED -> -(DEC_FAST_SPEED-1)
    if (decSpeed == 0) {
      decSpeed = DEC_FAST_SPEED;
      decSleep(false); // awake it
      digitalWrite(decDirPin, HIGH);
    } else if (decSpeed == DEC_FAST_SPEED) {
      decSpeed = (DEC_FAST_SPEED - 1);
      digitalWrite(decDirPin, LOW); // change direction
      // decSleep(false); // awake NOT NEEDED since it is already awake
    } else if  (decSpeed == (DEC_FAST_SPEED - 1)) {
      decSpeed = 0; // stop
      decSleep(true); // go to low power consumption
    }
    printLogUL(decSpeed);
    decLastTime = micros(); // reset Dec time
  }

  // Check if message on serial input
  if (Serial.available() > 0) {

    unsigned long slewTime = micros(); // record when slew code starts, RA 1x movement will be on hold hence we need to fix the gap later on

    input[in] = Serial.read(); 
  
    if (input[in] == '#' || input[in] == '\n') { // time to check what is in the buffer
      if ((input[0] == '+' || input[0] == '-' 
        || input[0] == 's' || input[0] == 'g')) { // agoto
        agoto(input);
      } else if (input[0] == ':') { // lx200
        printLog(input);
        lx200(input);
      } else {
        // unknown command, print message only
        // if buffer contains more than one chars
        // since stellarium seems to sedn extra #'s
        if (in > 0) Serial.println("String unknown. Expected lx200 or aGoto commands");
      }
      in = 0; // reset buffer
    } else {
      if (in++>20) in = 0; // prepare for next char or reset buffer if max lenght reached
    }
    
    /*
  
    String s = Serial.readString();    // FIXME: evaluate switch to char buffer to improve performance
    // printLog(s);
    if (s.charAt(0) == '#' || s.charAt(0) == ':') { // assume lx200 protocol
      lx200(s);
    } else if ((s.charAt(0) == '+' || s.charAt(0) == '-' 
             || s.charAt(0) == 's' || s.charAt(0) == 'g')) { // assume aGoto protocol
      agoto(s);
    } else {
      Serial.println("String unknown. Expected lx200 or aGoto commands");
    }
    */
    // if slewing took more than 5 secs, adjust RA
    slewTime = micros() - slewTime; // time elapsed for slewing
    if ( slewTime > (5 * 1000000) ) {
      printLog("*** adjusting Ra by secs: ");
      printLogUL(slewTime / 1000000);
      slewRaDecBySecs(slewTime / 1000000, 0); // it's the real number of seconds!
      printLog("*** adjusting Ra done");
    }
    // FIXME - this will slow down RA every time a string is received,
    //    i.e. Stellarium asking for current position, but perhaps it is not noticeable
    // raLastTime = micros(); // reset time 
    
  }

}

// Helpers to write on serial when DEBUG is active
void printLog(  String s)         { if (DEBUG) { Serial.print(":::");Serial.println(s); } }
void printLogL( long l)           { if (DEBUG) { Serial.print(":::");Serial.println(l); } }
void printLogUL(unsigned long ul) { if (DEBUG) { Serial.print(":::");Serial.println(ul);} }