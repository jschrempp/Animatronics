/*
 * AnimatronicEyes 
 * Part of the animatronic exploration of Team Practical Projects
 * https://github.com/TeamPracticalProjects
 * 
 * This is a hack of Nilheim Mechatronics code to see how we like it. His eye mechanism
 * is very cool. https://www.instructables.com/Simplified-3D-Printed-Animatronic-Dual-Eye-Mechani/
 * 
 * Pins
 *    A5: signal (3.3 volts/gnd) from the mouth processor.  Asserted (+3.3 volts) when
 *      the eyes should start a welcome sequence, and unasserts when the eyes can terminate
 *      the welcome sequence and return to "sleeping".
 *
 * (cc) Share Alike - Non Commercial - Attibution
 * 2022 Bob Glicksman and Jim Schrempp
 * 
 *      Now using mouth state machine as the default algorithm
 * v2.0 added second speak function, invoked by cloud function "event algorithm" set to 2
 *      faster eyes sample rate from 25ms to 10ms
 *      altered some variable names in processEvents(). No function change 
 *      temporal filtering now allows a bit of spatial jittering. fixes "goodbye" while standing in front of head
 * v1.9 temporal filtering now requires 2 frames of the same x,y 
 *      calibration now looks for several close frames
 *      changed pretty print x titles of calibration array to be correct 
 *      added temporal filtering
 * v1.8 changed the TOF upload time in loop to be longer (25 ms)
 * v1.7 add TOF event processing
 * v1.6 Exponential decay on the servo moves
 * v1.5 Added time of flight sensor
 * v1.4 Added kill switch to stop the eyes. Also changed wake up to be more realistic.
 * v1.3 Changed eye lid constants so that they are not coupled between left and right - now
 *      each lid has its own settings. 
 * v1.2 Added control on pin A5. A5 going HIGH terminates current sequence and starts a more
 *      attentive sequence. When A5 going LOW should sleep the eyes for five seconds, then return
 *      to normal activity.
 *      Moved the servo settings to eyeservosettings.h
 *      During idle times eyes now mostly sleep with a wakeup every IDLE_SEQUENCE_MIN_WAIT_MS
 * v1.1 Now with idle eye movements and a wake up sequence
 * v1.0 First checkin with everything working to do a small 8 step animation
 *    
 */ 

#include <Wire.h>
#include <TPPAnimationList.h>
#include <TPPAnimatePuppet.h>
#include <eyeservosettings.h>
#include <TPP_TOF.h>
#include <TPP_Animatronic_Global.h>

const String version = "2.0";

//SYSTEM_MODE(MANUAL);
SYSTEM_THREAD(ENABLED);  // added this in an attempt to get the software timer to work. didn't help

// Only ONE of these, please
#define TOF_USE 1
//#define VERIFY_CALIBRATION_ONLY 0

TPP_TOF theTOF;

#define DEBUGON
#define TRIGGER_PIN A5
#define KILL_BUTTON_PIN A4

const long IDLE_SEQUENCE_MIN_WAIT_MS = 10000; //30 sec // during idle times, random activity will happen longer than this
const long TOF_SAMPLE_TIME = 10;   // the TOF only updated 10x/sec, so don't need to upload the TOF data very often

#ifdef DEBUGON
    SerialLogHandler logHandler1(LOG_LEVEL_INFO, {  // Logging level for non-application messages LOG_LEVEL_ALL or _INFO
        { "app.main", LOG_LEVEL_ALL }               // Logging for main loop
        ,{ "app.puppet", LOG_LEVEL_WARN }               // Logging for Animate puppet methods
        ,{ "app.anilist", LOG_LEVEL_ERROR }               // Logging for Animation List methods
        ,{ "app.aniservo", LOG_LEVEL_INFO }          // Logging for Animate Servo details
        ,{"comm", LOG_LEVEL_ERROR}         // particle communication system 
        ,{"app.TOF", LOG_LEVEL_WARN}
    });
#else
    SerialLogHandler logHandler1(LOG_LEVEL_ERROR, {  // Logging level for non-application messages LOG_LEVEL_ALL or _INFO
        { "app.main", LOG_LEVEL_ERROR }               // Logging for main loop
        ,{ "app.puppet", LOG_LEVEL_ERROR }               // Logging for Animate puppet methods
        ,{ "app.anilist", LOG_LEVEL_ERROR }               // Logging for Animation List methods
        ,{ "app.aniservo", LOG_LEVEL_ERROR }          // Logging for Animate Servo details
        ,{"comm.protocol", LOG_LEVEL_WARN}          // particle communication system 
        ,{"comm.dtls", LOG_LEVEL_ERROR}          // particle communication system 
        ,{"app.TOF", LOG_LEVEL_TRACE}
        
    });
#endif

Logger mainLog("app.main");

// This is the master class that holds all the objects to be controlled
animationList animation1;  // When doing a programmed animation, this is the list of
                           // scenes and when they are to be played

// Servo Numbers for the Servo Driver board
#define X_SERVO 0
#define Y_SERVO 1
#define L_UPPERLID_SERVO 2
#define L_LOWERLID_SERVO 3
#define R_UPPERLID_SERVO 4
#define R_LOWERLID_SERVO 5

//------------- XXX processEvents ------------------
////////// NO LONGER USED. TO BE REMOVED IN A FUTURE COMMIT
// evaluate the TOF sensor results to determine if a mouth event is to be published, and publish the resulting event
void processEvents(pointOfInterest POI) {

    // local constants
    const long TOO_CLOSE_MM = 254;  // object is too close if < 254 mm = 10"
    const unsigned long SUPPRESS_TOO_CLOSE_MS = 10000;    // time out for repeat of too close event - 10 seconds
    const unsigned long VALID_ENGAGEMENT_MS = 15000;   // 15 sec is the minimum time for a "valid" engagement

    // local variables
    static bool personInFOV = false;   // true of the eyes were open the last time through
    static unsigned long personEnteredFOVMS = millis();
    static unsigned long timeTooClose = 0;
    String eventTypeAsString = "";


    if (POI.gotNewSensorData) {
                
        // test to see if the sensor detects a valid object in the fov
        if (POI.hasDetection) {     // valid object in fov
            if(personInFOV == false) {    
                // someone just entered the fov; send entered event
                personInFOV = true;    // note that person is in FOV
                personEnteredFOVMS = millis();
                eventTypeAsString = String(Person_entered_fov);
                Particle.publish("TOF_event", eventTypeAsString);
                mainLog.trace("EVENT: Person Entered FOV");
                return;
            }
            else {      
                // someone is in the fov for two invocations, test for too close
                if( (POI.distanceMM < TOO_CLOSE_MM) && ((millis() - timeTooClose) > SUPPRESS_TOO_CLOSE_MS) ) {
                    // don't do this too often
                    timeTooClose = millis();
                    eventTypeAsString = String(Person_too_close);
                    Particle.publish("TOF_event", eventTypeAsString); 
                    mainLog.trace("EVENT: Person Too Close"); 
                    return;             
                }
            }
        }
        else {      
            //no valid object in fov
            if(personInFOV == true) {  
                // Person has left FOV
                personInFOV = false;   // note that FOV is empty
                if( (millis() - personEnteredFOVMS) > VALID_ENGAGEMENT_MS) {   
                    // person  in fov for a "decent" amount of time
                    eventTypeAsString = String(Person_left_fov);
                    Particle.publish("TOF_event", eventTypeAsString);  
                    mainLog.trace("Person Left FOV");
                    return; 
                }
                else {      // person in fov for only a short time
                    eventTypeAsString = String(Person_left_quickly);
                    Particle.publish("TOF_event", eventTypeAsString);  
                    mainLog.trace("Person Left FOV Quickly");
                    return;
                }

            }
            return;
        }
    }
    // if nothing to do, just return
    return;

}   // end of processEvents()

void processEventsStateMachine(bool hasDetection, int distanceMM) {

    enum headStates {
        hs_idle  = 1,
        hs_normal = 2,
        hs_too_close = 3,
        hs_person_left = 4
    };
    static headStates currentState = hs_idle;
    String headStatesStrings[4] = {"Idle","Normal","Too Close","Person left"};

    // local constants
    const long TOO_CLOSE_MM = 254;  // object is too close if < 254 mm = 10"
    const unsigned long VALID_ENGAGEMENT_MS = 15000;   // 15 sec is the minimum time for a "valid" engagement
    const long MIN_TIME_FOR_NEW_WELCOME = 5000;  // don't welcome more frequently than this

    // local variables
    bool personTooClose = false;
    unsigned long currentMS = millis();
    static unsigned long stateStartMS = 0;
    unsigned long timeInStateMS = currentMS - stateStartMS;
    TOF_detect speakThisEvent = No_event;

    if (hasDetection && (distanceMM < TOO_CLOSE_MM)) {
        personTooClose = true;
    }

    switch(currentState) {
        case hs_idle:
            if (!hasDetection ) {
                // stay in this state
            } else {
                stateStartMS = currentMS;
                if (personTooClose) {
                    // speak too close event
                    speakThisEvent = Person_too_close;
                    // change state
                    currentState = hs_too_close;
                } else {
                    // speak welcome event
                    speakThisEvent = Person_entered_fov;
                    // change state
                    currentState = hs_normal;
                }
            }
            break;

        case hs_normal:
            if (hasDetection) {
                if (personTooClose) {
                    // speak too close
                    speakThisEvent = Person_too_close;
                    // change state
                    currentState = hs_too_close;
                } else {
                    // stay in this state
                }
            } else {
                // no detection
                if (timeInStateMS < VALID_ENGAGEMENT_MS) {
                    // speak quick goodbye
                    speakThisEvent = Person_left_quickly;
                    // reset timer
                    stateStartMS = currentMS;
                    // change state
                    currentState = hs_person_left;
                } else {
                if (timeInStateMS >= VALID_ENGAGEMENT_MS) {
                    // speak normal goodbye
                    speakThisEvent = Person_left_fov;
                    // reset timer
                    stateStartMS = currentMS;
                    // change state
                    currentState = hs_person_left;
                    }
                }
            }
            break;

        case hs_too_close:
            if (hasDetection) {
                if (!personTooClose) {  //xxx
                    // speak nothing
                    // change state
                    currentState = hs_normal;
                } else {
                    // stay in this state
                }
            } else {
                // no detection
                if (timeInStateMS < VALID_ENGAGEMENT_MS) {
                    // speak quick goodbye
                    speakThisEvent = Person_left_quickly;
                    // reset timer
                    stateStartMS = currentMS;
                    // change state
                    currentState = hs_person_left;
                } else {
                    // speak normal goodbye
                    speakThisEvent = Person_left_fov;
                    // reset timer
                    stateStartMS = currentMS;
                    // change state
                    currentState = hs_person_left;
                }
            }
            break;

        case hs_person_left:
            if (hasDetection) {
                // speak nothing
                //currentState = hs_normal;
            } else {
                // no detection
                if (timeInStateMS < MIN_TIME_FOR_NEW_WELCOME ) {
                    // stay in this state so we don't welcome again
                } else {
                    // speak nothing
                    // change state
                    currentState = hs_idle;
                }
            }
            break;
        default:
            break;
    } // end of switch
    
    // send event to the mouth
    if (speakThisEvent != No_event) {
        String eventTypeAsString = String(speakThisEvent);
        publishEvent("TOF_event", eventTypeAsString); 
        mainLog.trace("Event sent: " + eventTypeAsString);
    }

    // logging
    static headStates lastLoggedState;
    if (lastLoggedState != currentState) {
        lastLoggedState = currentState;
        mainLog.trace("HeadState: " + headStatesStrings[currentState-1]);
    }

    return;

}   // end of processEventsStateMachine()


//------- midValue --------
// Pass in two ints and this returns the value in the middle of them.
int midValue(int value1, int value2) {
  
    if (value1 == value2){
        return value1;
    }

    int halfway = abs(value1 - value2)/2;
    
    if (value1 > value2) {
        return value2 + halfway;
    } else {
        return value1 + halfway;
    }

}

// This timer is used to pulse the top level object process() method
// which then gets passed down all the way to the AnimateServo library.
// This timer allows the servos to continue to move even when the main
// code is in a delay() function.
//Timer animationTimer(500, animationTimerCallback);  
//Timer animationTimer(5, &animationList::process, animation1); 

// when called from a timer it crashes
void animationTimerCallback() {

    // now have animation pass this on to all the servos it manages
    volatile static bool inCall = false;
    if (!inCall) {
        inCall = true;
        animation1.process();
        inCall = false;
    }
}

void publishEvent(String eventName, String eventData) {

    static unsigned long lastPublishedMS = 0;
    if (millis() - lastPublishedMS > 1000) {
        Particle.publish(eventName, eventData);
        lastPublishedMS = millis();
    } else {
        mainLog.error("Publication suppressed, too fast: " + eventName + "/" + eventData );
    }

}


// Cloud functions must return int and take one String
int restartDevice(String extra) {
    System.reset();
    return 0;
}


//------ setup -----------
void setup() {

    pinMode(TRIGGER_PIN, INPUT);
    pinMode(KILL_BUTTON_PIN,INPUT_PULLUP);

    pinMode(D7, OUTPUT);

    Particle.function("restart device", restartDevice);

    delay(1000);
    mainLog.info("===========================================");
    mainLog.info("===========================================");
    mainLog.info("Animate Eye Mechanism");
    
    animation1.puppet.eyeballs.init(X_SERVO,X_POS_MID,X_POS_LEFT_OFFSET,X_POS_RIGHT_OFFSET,
            Y_SERVO, Y_POS_MID, Y_POS_UP_OFFSET, Y_POS_DOWN_OFFSET);

    animation1.puppet.eyelidLeftUpper.init(L_UPPERLID_SERVO, LEFT_UPPER_OPEN, LEFT_UPPER_CLOSED);
    animation1.puppet.eyelidLeftLower.init(L_LOWERLID_SERVO, LEFT_LOWER_OPEN, LEFT_LOWER_CLOSED);
    animation1.puppet.eyelidRightUpper.init(R_UPPERLID_SERVO, RIGHT_UPPER_OPEN, RIGHT_UPPER_CLOSED);
    animation1.puppet.eyelidRightLower.init(R_LOWERLID_SERVO, RIGHT_LOWER_OPEN, RIGHT_LOWER_CLOSED);


    // Establish Animation List

    animation1.addScene(sceneEyesAhead, -1, MOVE_SPEED_IMMEDIATE, -1);
    animation1.addScene(sceneEyesOpen, 0, MOVE_SPEED_IMMEDIATE, 0);
   
    // xxx using the timer causes a crash
    // Start the animation timer 
    //animationTimer.start(); 

    animation1.startRunning();
    animation1.process();

#ifdef VERIFY_CALIBRATION_ONLY

    // Just show that the eye servo settings work
    sequenceCalibrationConfirmation();

#elif TOF_USE

    // Time of Flight Sensor set up
    Wire.begin(); //This resets to 100kHz I2C
    Wire.setClock(400000); //Sensor has max I2C freq of 400kHz 
    
    theTOF.initTOF();

    sequenceCalibrationConfirmation();
    animation1.startRunning();

#else

    sequenceLookReal();
    sequenceAsleep(1000);

#endif


    
}

//------- MAIN LOOP --------------
void loop() {

    static bool firstLoop = true;
    static bool startingUp = true;

    if (firstLoop){

        firstLoop = false;
        mainLog.info("first time in main loop");

    }

    // this is called every time to make the animation run
    animationTimerCallback();

    if (startingUp) {
        // keep coming here until start up sequence is done
        if (!animation1.isRunning()) {
            startingUp = false;
            mainLog.info("finished start up sequence");
        }
        return;
    }


#ifdef TOF_USE
    //int32_t smallestValue; 
    int32_t focusX = -255;  //sensor coordinates
    int32_t focusY = -255;
    static int32_t xPos = -1;
    static int32_t yPos = -1;
    static long lastEyeUpdateMS = 0;

    //decide where to point the eyes
    if ( (millis() - lastEyeUpdateMS) > TOF_SAMPLE_TIME){    // XXX made this longer than 1 ms

        // this is called every time to allow TOF to make measurements
        pointOfInterest thisPOITF;

        //theTOF.getPOI(&thisPOI);
        theTOF.getPOITemporalFiltered(&thisPOITF);

        if (thisPOITF.gotNewSensorData) {
           
            // consider running the mouth
            processEventsStateMachine(thisPOITF.hasDetection, thisPOITF.distanceMM);
            
        }


        // get POI data without temporal filtering
        pointOfInterest thisPOI;
        theTOF.getPOI(&thisPOI);

        // do we have a focus point?
        if (thisPOI.hasDetection) {

            focusX = thisPOI.x;
            focusY = thisPOI.y;

            lastEyeUpdateMS = millis();

            int xPosNew = map(focusX,0,7, 0,100);   
            int yPosNew = map(focusY,0,7, 100,0);
            
            // has the focus changed?
            if ((xPosNew != xPos) || (yPosNew != yPos)) {

                xPos = xPosNew;
                yPos = yPosNew;

                //mainLog.info("New position: x: %d, y: %d",focusX,focusY);

                animation1.stopRunning();
                animation1.clearSceneList();
                animation1.addScene(sceneEyesOpen, 100 , MOVE_SPEED_IMMEDIATE, -1);
                animation1.addScene(sceneEyesLeftRight, xPos, MOVE_SPEED_IMMEDIATE, -1);
                animation1.addScene(sceneEyesUpDown, yPos, MOVE_SPEED_IMMEDIATE, 0);

                //now let the animation run
                animation1.startRunning();
            }
        } 
    }

    // If the POI has not changed, then go to sleep
    if (millis() - lastEyeUpdateMS > 2000){

        lastEyeUpdateMS = millis();

        animation1.stopRunning();
        animation1.clearSceneList();
        animation1.addScene(sceneEyesOpen, 0 , MOVE_SPEED_IMMEDIATE, 0);
        
        //now let the animation run
        animation1.startRunning();
    }

#else

#ifndef VERIFY_CALIBRATION_ONLY

    static bool mouthTriggered = false;
    static long lastIdleSequenceStartTime = 0;

    static bool weAreAlive = true; // when true we will not run

   // has the sleep button been pressed?
    static int lastKillButtonState = switchReadStateBUTTON_PIN();
    if(switchReadStateBUTTON_PIN() != lastKillButtonState){

        lastKillButtonState = switchReadStateBUTTON_PIN();
        // invert alive/dead state
        mainLog.info("kill button pressed");

        if (weAreAlive){
            mainLog.info("we are now going to die");
            weAreAlive = false;
            animation1.stopRunning();
            animation1.clearSceneList();
            sequenceAsleep(1000);
            animation1.startRunning();
        } else {
            mainLog.info("we are now alive again");
            weAreAlive = true;
        }
        
    }

    if (!weAreAlive){
        //we're dead so do nothing else
        return;
    }

 

    // have we been triggered by the mouth?
    if (digitalRead(TRIGGER_PIN) == HIGH) {
        
        if (mouthTriggered) {
            // we are already running, refresh the sequence if needed
            if (!animation1.isRunning()) {
                mainLog.info("triggered refresh");
                animation1.stopRunning();
                animation1.clearSceneList();
                sequenceEyesRoamAhead();
                animation1.startRunning();
            } 
        } else {
            // we have been triggered, start the sequence
            mouthTriggered = true;
            //start the appropriate sequence
            mainLog.info("eyes triggered");
            animation1.stopRunning();
            animation1.clearSceneList();
            sequenceEyesRoamAhead();
            animation1.startRunning();
        } 

    } else {

        // trigger pin is low
        if (!mouthTriggered) {
            // nothing to do, we are already stopped

        } else {
            //trigger has gone away
            mouthTriggered = false;
            mainLog.info("trigger stop and set asleep");
            // stop the sequence and go to sleep sequence
            animation1.stopRunning();
            animation1.clearSceneList();
            sequenceAsleep(30000);
            animation1.startRunning();
        }

    }

    // We are not mouth triggered, so decide if we want to have 
    // the puppet do some random thing
    if (!mouthTriggered) {
        
        if (!animation1.isRunning() && 
            (millis() - lastIdleSequenceStartTime > IDLE_SEQUENCE_MIN_WAIT_MS)) {
            // there is no animation running, and we haven't done any random
            // thing for at least IDLE_SEQUENCE_MIN_WAIT_TS

            animation1.clearSceneList();
            lastIdleSequenceStartTime = millis();

            int thisRandom = random(100);
            if (thisRandom > 80) {
                //20%
                sequenceWakeUpSlowly(0);
                sequenceEyesRoam();
                sequenceAsleep(5000);
                mainLog.info("Idle option 1");
            } else if (thisRandom > 60){
                //20%
                sequenceWakeUpSlowly(0);
                sequenceEyesRoam();
                sequenceAsleep(5000);
                mainLog.info("Idle option 2");
            } else if (thisRandom > 20){
                //20%
                sequenceEyesRoamAhead();
                sequenceEyesRoamAhead();
                sequenceEyesRoamAhead();
                sequenceEyesRoamAhead();
                sequenceAsleep(5000);
                mainLog.info("Idle option 3");
            } else if (thisRandom > 0){
                //20%
                sequenceBlinkEyes(1000);
                sequenceBlinkEyes(100);
                sequenceAsleep(5000);
                mainLog.info("Idle option 4");
            }

            animation1.startRunning();
        }
    }

#endif
#endif


} // end of main loop

void sequenceCalibrationConfirmation() {

    sequenceBlinkEyes(500);
    mainLog.info("CALIBRATION test: eyes ahead, open");
    animation1.addScene(sceneEyesAheadOpen, -1, MOVE_SPEED_IMMEDIATE, 1000);

    sequenceBlinkEyes(500);

    mainLog.info("CALIBRATION test: eyes right");
    animation1.addScene(sceneEyesLeftRight, EYES_RIGHT, MOVE_SPEED_SLOW, 1000);
    mainLog.info("CALIBRATION test: eyes left");
    animation1.addScene(sceneEyesLeftRight, EYES_LEFT, MOVE_SPEED_SLOW, 1000);
    mainLog.info("CALIBRATION test: eyes x mid");
    animation1.addScene(sceneEyesLeftRight, EYES_X_MID, MOVE_SPEED_SLOW, 1000);

    sequenceBlinkEyes(500);

    mainLog.info("CALIBRATION test: eyes right");
    animation1.addScene(sceneEyesLeftRight, EYES_RIGHT, MOVE_SPEED_FAST, 200);
    mainLog.info("CALIBRATION test: eyes left");
    animation1.addScene(sceneEyesLeftRight, EYES_LEFT, MOVE_SPEED_FAST, 200);
    mainLog.info("CALIBRATION test: eyes x mid");
    animation1.addScene(sceneEyesLeftRight, EYES_X_MID, MOVE_SPEED_FAST, 200);
    
    sequenceBlinkEyes(500);
    animation1.addScene(sceneEyesAhead, -1, MOVE_SPEED_SLOW, 1000);

    mainLog.info("CALIBRATION test: eyes up");
    animation1.addScene(sceneEyesUpDown, EYES_UP, MOVE_SPEED_SLOW, 1000);
    mainLog.info("CALIBRATION test: eyes down");
    animation1.addScene(sceneEyesUpDown, EYES_DOWN, MOVE_SPEED_SLOW, 1000);
    mainLog.info("CALIBRATION test: eyes y mid");
    animation1.addScene(sceneEyesUpDown, EYES_Y_MID, MOVE_SPEED_SLOW, 1000);

    sequenceBlinkEyes(500);
    animation1.addScene(sceneEyesAhead, -1, MOVE_SPEED_SLOW, 1000);

    sequenceBlinkEyes(200);
    sequenceBlinkEyes(500);

}


void sequenceGeneralTests () {

    sequenceAsleep(2000);

    sequenceWakeUpSlowly(5000);
      
    animation1.addScene(sceneEyesAheadOpen, -1, MOVE_SPEED_SLOW, 0);

    animation1.addScene(sceneEyesLeftRight, 0, MOVE_SPEED_SLOW, 0);
    animation1.addScene(sceneEyesLeftRight, 100, MOVE_SPEED_SLOW, 0);
    animation1.addScene(sceneEyesLeftRight, 0, MOVE_SPEED_SLOW, 0);
    animation1.addScene(sceneEyesLeftRight, 100, MOVE_SPEED_SLOW, 0);
    animation1.addScene(sceneEyesLeftRight, 0, MOVE_SPEED_SLOW, 0);

    animation1.addScene(sceneEyesLeftRight, 100, MOVE_SPEED_FAST, 0);
    animation1.addScene(sceneEyesLeftRight, 0, MOVE_SPEED_FAST, 0);
    animation1.addScene(sceneEyesLeftRight, 100, MOVE_SPEED_FAST, 0);
    animation1.addScene(sceneEyesLeftRight, 0, MOVE_SPEED_FAST, 0);

    animation1.addScene(sceneEyesAhead, -1, MOVE_SPEED_SLOW, 0);

    animation1.addScene(sceneEyesUpDown, EYES_UP, MOVE_SPEED_SLOW, 0);
    animation1.addScene(sceneEyesUpDown, EYES_DOWN, MOVE_SPEED_SLOW, 0);
    animation1.addScene(sceneEyesUpDown, EYES_UP, MOVE_SPEED_SLOW, 0);
    animation1.addScene(sceneEyesUpDown, EYES_DOWN, MOVE_SPEED_SLOW, 0);
    animation1.addScene(sceneEyesUpDown, EYES_UP, MOVE_SPEED_SLOW, 0);
    animation1.addScene(sceneEyesUpDown, EYES_DOWN, MOVE_SPEED_SLOW, 0);
    animation1.addScene(sceneEyesUpDown, EYES_UP, MOVE_SPEED_SLOW, 0);
    animation1.addScene(sceneEyesUpDown, EYES_DOWN, MOVE_SPEED_SLOW, 0);



    animation1.addScene(sceneEyesAhead, -1, MOVE_SPEED_SLOW, 0);

    sequenceBlinkEyes(1000);
    sequenceBlinkEyes(1000);
    sequenceBlinkEyes(1000);
    sequenceBlinkEyes(1000);
    sequenceBlinkEyes(1000);


    
    

/*

    animation1.addScene(sceneEyesApuppet, MOVE_SPEED_FAST, -1);
    animation1.addScene(sceneEyesOpenWide,MOVE_SPEED_FAST,500);

    //animation1.addScene(sceneBlink, MOVE_SPEED_SLOW, 500);
    //animation1.addScene(sceneBlink, MOVE_SPEED_SLOW, 500);

    animation1.addScene(sceneEyesClosed, MOVE_SPEED_FAST, -1);
    animation1.addScene(sceneEyesApuppet, MOVE_SPEED_FAST, 1000);

    animation1.addScene(sceneEyesRight, MOVE_SPEED_FAST, -1);
    animation1.addScene(sceneEyesOpenWide, MOVE_SPEED_FAST, 2000);

    animation1.addScene(sceneEyesOpen, 5, -1);
    animation1.addScene(sceneEyesApuppet, 5, 1000);


    animation1.addScene(sceneEyesRight, MOVE_SPEED_FAST, -1);
    animation1.addScene(sceneEyesUp, MOVE_SPEED_SLOW, -1);
    animation1.addScene(sceneEyesOpenWide, MOVE_SPEED_SLOW, 0);
    animation1.addScene(sceneEyesOpen, MOVE_SPEED_FAST, -1);
    animation1.addScene(sceneEyesDown,5,-1);
    animation1.addScene(sceneEyesLeft, 5, 0);

 */

    sequenceEndStandard();

}

void sequenceLookReal() {

    sequenceWakeUpSlowly(0);

}

void sequenceWakeUpSlowly(int delayAfterMS) {

    sequenceAsleep(3000);

    sequenceEyesWake(delayAfterMS); 

}


void sequenceAsleep(int delayAfterMS) {

    animation1.addScene(sceneEyesAhead, -1, MOVE_SPEED_IMMEDIATE, -1);
    animation1.addScene(sceneEyesOpen, 0, MOVE_SPEED_SLOW, delayAfterMS);
    animation1.addScene(sceneEyesOpen, 0, MOVE_SPEED_IMMEDIATE, 0); // need this so animation is still "running" after the 
                                                                  // previous call with a delay.

}

void sequenceEyesWake(int delayAfterMS){

    int thisRandom = random(3);
    switch (thisRandom) {
        case 0:
            animation1.addScene(sceneEyelidsLeft, eyelidSlit, .1, -1);
            break;
        case 1: 
            animation1.addScene(sceneEyelidsRight, eyelidSlit, .1, -1);
            break;
        case 2:
            animation1.addScene(sceneEyelidsLeft, eyelidSlit, .1, -1);
            animation1.addScene(sceneEyelidsRight, eyelidSlit, .1, -1);
            break;

    }
    animation1.addScene(sceneEyesLeftRight, 0, .2, 1000);
    animation1.addScene(sceneEyesLeftRight, 100, .2, 2000);
    animation1.addScene(sceneEyesLeftRight, 50, .5, -1);
    animation1.addScene(sceneEyelidsLeft, eyelidClosed, .2, 1000);
    animation1.addScene(sceneEyelidsRight, eyelidClosed, .2, 1000);
    animation1.addScene(sceneEyesLeftRight, 75, .2, -1);
    animation1.addScene(sceneEyesOpen, eyelidSlit, .1, -1);
    animation1.addScene(sceneEyesLeftRight, 35, .2, 2000);
    animation1.addScene(sceneEyesOpen, eyelidClosed, .1,2000);
    animation1.addScene(sceneEyesLeftRight, 50, .4, -1);
    animation1.addScene(sceneEyesOpen, eyelidNormal, .5, 0);
    sequenceBlinkEyes(delayAfterMS);

}

void sequenceEyesRoam() {
    // Eyes roam with saccade between several points 
    // (this isn't done yet)

    static int posLeftRight = 50;
    static int posUpDown = 50;

    randomSeed(micros());

    for (int i=0; i<30; i++){

        // pick left/right and up/down
        //posLeftRight = posLeftRight + random(2,20) - 9;
        //posUpDown = posUpDown + random(2,20) - 9;
        posLeftRight = random(10,90);
        posUpDown = random(25,75);

        float speed = random(1,20) / 10.0;
        int delay = random(500,1000);

        if(random(0,100) > 80){
            sequenceBlinkEyes(-1);
        }
        animation1.addScene(sceneEyesLeftRight, posLeftRight, speed, -1);
        animation1.addScene(sceneEyesUpDown, posUpDown, speed, delay);
        
    }

}

void sequenceEyesRoamAhead() {
    // Eyes basically look ahead, but saccade 
    // this creates a sequence of 30 scenes
    randomSeed(micros());

    animation1.addScene(sceneEyesOpen,100,100,-1);

    for (int i=0; i<30; i++){

        // pick left/right and up/down
        int posLeftRight = random(20,80);
        int posUpDown = random(40,60);

        float speed = random(1,20) / 10.0;
        int delay = random(200,400);

        if(random(0,100) > 90){
            sequenceBlinkEyes(-1);
        }
        animation1.addScene(sceneEyesLeftRight, posLeftRight, speed, -1);
        animation1.addScene(sceneEyesUpDown, posUpDown, speed, delay);
        
    }

}

void sequenceEndStandard() {

    animation1.addScene(sceneEyesAhead, -1, 3, -1);
    animation1.addScene(sceneEyesOpen, 50, 1, 100);
}

void sequenceBlinkEyes(int delayAfterMS) {

    // another way to do it
    //animation1.addScene(sceneEyesOpen, eyelidClosed, MOVE_SPEED_IMMEDIATE, 0);
    //animation1.addScene(sceneEyesOpen, eyelidNormal, MOVE_SPEED_IMMEDIATE, delayAfterMS);

    animation1.addScene(sceneEyelidsRight, eyelidClosed, MOVE_SPEED_IMMEDIATE, -1);
    animation1.addScene(sceneEyelidsLeft, eyelidClosed, MOVE_SPEED_IMMEDIATE, 0);
    animation1.addScene(sceneEyelidsRight, eyelidNormal, MOVE_SPEED_IMMEDIATE, -1);
    animation1.addScene(sceneEyelidsLeft, eyelidNormal, MOVE_SPEED_IMMEDIATE, delayAfterMS);
    
}

//---------- buttonWasPushedBUTTON_PIN 
// Returns true if BUTTON_PIN goes HIGH to LOW
bool buttonWasPushedBUTTON_PIN() {
    
    static int lastSwitchState = HIGH;
    int retCode = false;

    // only return true if the button state goes HIGH to LOW
    int switchState = switchReadStateBUTTON_PIN();
    if ( switchState == LOW) {
    if (lastSwitchState == HIGH) {
        // Switch was HIGH, now LOW
        retCode = true;
    }
    }
    lastSwitchState = switchState; 
    return retCode;
}

//-------- switchReadStateBUTTON_PIN
// returns the debounced value of BUTTON_PIN
int switchReadStateBUTTON_PIN() {

    static int switchState = HIGH;   // the value of the switch that is returned
    static int lastButtonState = HIGH;   // the previous reading from the input pin
    static unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
    static bool firstTime = true;     // first time called since starting
    unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers

    if (firstTime){
        firstTime = false;
        // init to what the button is now
        lastButtonState =  digitalRead(KILL_BUTTON_PIN);
        switchState = lastButtonState;
    }

    // read the state of the switch into a local variable:
    int reading = digitalRead(KILL_BUTTON_PIN);

    // If the switch changed, due to noise or pressing:
    if (reading != lastButtonState) {
        // reset the debouncing timer
        lastDebounceTime = millis();
        lastButtonState = reading; // remember what value we just saw
    }

    if ((millis() - lastDebounceTime) > debounceDelay) {
        // whatever the reading is at, it's been there for longer than the debounce
        // delay, so take it as the actual current state:
        switchState = reading;
    }

    return switchState;
}