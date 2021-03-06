/*
 * AnimatroicEyes 
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
 * 2020 Bob Glicksman and Jim Schrempp
 * 
 * v1.2 Added control on pin A5. A5 going HIGH terminates current sequence and starts a more
 *      attentive sequence. When A5 going LOW should sleep the eyes for five seconds, then return
 *      to normal activity.
 * v1.1 Now with idle eye movements and a wake up sequence
 * v1.0 First checkin with everything working to do a small 8 step animation
 *    
 */ 


const String version = "1.2";
 
//SYSTEM_MODE(MANUAL);
SYSTEM_THREAD(ENABLED);  // added this in an attempt to get the software timer to work. didn't help

#include <Wire.h>
#include <TPPAnimationList.h>
#include <TPPAnimatePuppet.h>
#include <eyeservosettings.h>

#define CALLIBRATION_TEST 
#define DEBUGON
#define TRIGGER_PIN A5

const long IDLE_SEQUENCE_MIN_WAIT_MS = 120000; //2 min // during idle times, random activity will happen longer than this

SerialLogHandler logHandler1(LOG_LEVEL_INFO, {  // Logging level for non-application messages LOG_LEVEL_ALL or _INFO
    { "app.main", LOG_LEVEL_ALL }               // Logging for main loop
    ,{ "app.puppet", LOG_LEVEL_INFO }               // Logging for Animate puppet methods
    ,{ "app.anilist", LOG_LEVEL_ERROR }               // Logging for Animation List methods
    ,{ "app.aniservo", LOG_LEVEL_INFO }          // Logging for Animate Servo details
    ,{"comm.protocol", LOG_LEVEL_WARN}          // particle communication system 
});

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

//------ setup -----------
void setup() {

    pinMode(TRIGGER_PIN, INPUT);

    delay(1000);
    mainLog.info("===========================================");
    mainLog.info("===========================================");
    mainLog.info("Animate Eye Mechanism");
    
    animation1.puppet.eyeballs.init(X_SERVO,X_POS_MID,X_POS_LEFT_OFFSET,X_POS_RIGHT_OFFSET,
            Y_SERVO, Y_POS_MID, Y_POS_UP_OFFSET, Y_POS_DOWN_OFFSET);

    animation1.puppet.eyelidLeftUpper.init(L_UPPERLID_SERVO, LEFT_UPPER_OPEN, LEFT_UPPER_CLOSED);
    animation1.puppet.eyelidLeftLower.init(L_LOWERLID_SERVO, LEFT_LOWER_OPEN, LEFT_LOWER_CLOSED);
    animation1.puppet.eyelidRightUpper.init(R_UPPERLID_SERVO, RIGHT_UPPER_OFFSET - LEFT_UPPER_OPEN, RIGHT_UPPER_OFFSET - LEFT_UPPER_CLOSED);
    animation1.puppet.eyelidRightLower.init(R_LOWERLID_SERVO, RIGHT_LOWER_OFFSET - LEFT_LOWER_OPEN, RIGHT_LOWER_OFFSET - LEFT_LOWER_CLOSED);


    // Establish Animation List

    animation1.addScene(sceneEyesAhead, -1, MOVE_SPEED_IMMEDIATE, -1);
    animation1.addScene(sceneEyesOpen, 0, MOVE_SPEED_IMMEDIATE, 0);
   
    // xxx using the timer causes a crash
    // Start the animation timer 
    //animationTimer.start(); 

    animation1.startRunning();
    animation1.process();

    //sequenceGeneralTests();
    sequenceLookReal();
    sequenceAsleep(1000);
    
}

//------- MAIN LOOP --------------
void loop() {

    static bool firstLoop = true;
    static bool mouthTriggered = false;
    static long lastIdleSequenceStartTime = 0;

    if (firstLoop){

        firstLoop = false;
        mainLog.info("eyes start up");

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
                sequenceAsleep(5000);
                Particle.publish("Idle option 1");
            } else if (thisRandom > 60){
                //20%
                sequenceWakeUpSlowly(0);
                sequenceEyesRoam();
                sequenceAsleep(5000);
                Particle.publish("Idle option 2");
            } else if (thisRandom > 20){
                //20%
                sequenceEyesRoamAhead();
                sequenceEyesRoamAhead();
                sequenceEyesRoamAhead();
                sequenceEyesRoamAhead();
                sequenceAsleep(5000);
                Particle.publish("Idle option 3");
            } else if (thisRandom > 0){
                //20%
                sequenceBlinkEyes(1000);
                sequenceBlinkEyes(100);
                sequenceBlinkEyes(100);
                sequenceBlinkEyes(100);
                sequenceAsleep(5000);
                Particle.publish("Idle option 4");
            }

            animation1.startRunning();
        }
    }
    animationTimerCallback();

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
    animation1.addScene(sceneEyesOpen, 0, MOVE_SPEED_IMMEDIATE, delayAfterMS);
    animation1.addScene(sceneEyesOpen, 0, MOVE_SPEED_IMMEDIATE, 0); // need this so animation is still "running" after the                                                               // previous call with a delay.

}

void sequenceEyesWake(int delayAfterMS){

    animation1.addScene(sceneEyelidsLeft, eyelidSlit, .1, -1);
    animation1.addScene(sceneEyesLeftRight, 0, .2, 1000);
    animation1.addScene(sceneEyesLeftRight, 100, .2, 2000);
    animation1.addScene(sceneEyesLeftRight, 50, .5, -1);
    animation1.addScene(sceneEyelidsLeft, eyelidClosed, .2, 1000);
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
        posLeftRight = random(25,75);
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
        int posLeftRight = random(40,60);
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