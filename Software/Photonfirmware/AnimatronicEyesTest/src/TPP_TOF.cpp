/*
    TPPTOF.cpp

    Team Practical Project Time of Flight sensor

    This library implements a TOF interface that selects points of interest within the
    TOF field of view. As objects in the field of view change, the points of interest
    will change. Our intent is to direct an animatronic eye mechanism with points it 
    should consider looking at. We leave the decision about how long to look at any 
    given point up to the caller.

    This firmware is based upon the example 1 code in the Sparkfun library.    
  
    Author: Bob Glicksman, Jim Schrempp
    (c) Copyright 2022 Bob Glicksman and Jim Schrempp

    This work is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.

2022 02 23  change to reduce chatter. 
2022 11 27  change to poi detection - must be closer than calibration distance
            getPOITemporalFiltered has better TRACE level logging

*/

#include <TPP_TOF.h>

SparkFun_VL53L5CX myImager;
VL53L5CX_ResultsData measurementData; // Result data class structure, 1356 byes of RAM

Logger theLogger("app.TOF");

// noise range in measured data.  Anything within +/- 50 of the calibrations is noise
const uint16_t NOISE_RANGE = 50;
const uint16_t MAX_CALIBRATION = 2000;  // anything greater is set to 2000 mm

// declare 8x8 array of calibration values
int32_t calibration[64];

#define RANGING_FREQUENCY 14  // times per second for sensor to sample the environment
#define FRAMES_FOR_GOOD_HIT 2 // number of subsequent frames needed to consider a hit good 
                              // this filters out spurious hits

int imageResolution; // read this back from the sensor
int imageWidth; // read this back from the sensor

// -------- initTOF ----------
// called once to initialize the sensor
// may take up to 10 seconds to return
// returns True if sensor was initialized; False for error
void TPP_TOF::initTOF(){

    imageResolution = 0; // read this back from the sensor
    imageWidth = 0; // read this back from the sensor

    Serial.println("SparkFun VL53L5CX Imager Example");
    
    Serial.println("Initializing sensor board. This can take up to 10s. Please wait.");
    if (myImager.begin() == false) {
        Serial.println(F("Sensor not found - check your wiring. Freezing"));
        while (1) {
            delay(10); // allow remote reset to happen
        } ;
    }
    
    myImager.setResolution(64); //Enable all 64 pads - 8 x 8 array of readings
    
    imageResolution = myImager.getResolution(); //Query sensor for current resolution - either 4x4 or 8x8
    imageWidth = sqrt(imageResolution); //Calculate printing width

    // debug print statement - are we communicating with the module
    String theResolution = "Resolution = ";
    theResolution += String(imageResolution);
    Serial.println(theResolution);

    // XXX test out target order and sharpener changes
    // myImager.setSharpenerPercent(20);
    // myImager.setTargetOrder(SF_VL53L5CX_TARGET_ORDER::CLOSEST);
    // myImager.setTargetOrder(SF_VL53L5CX_TARGET_ORDER::STRONGEST);

    myImager.setRangingFrequency(RANGING_FREQUENCY);

    myImager.startRanging();

    // fill in the calibration data array

    // wait for data to be ready
    do {
        // do nothing here, wait for data to be ready
        delay(5); //Small delay between polling
    } while(myImager.isDataReady() != true);

    // data is now ready

    // look for two successive frames that are similar
    bool gotSimilarFrames = false;
    int frameCount = 0;
    int sumOfDistances = 0;
    int lastFrameSum = 0;
    do {
        if (myImager.isDataReady()) {
            if(myImager.getRangingData(&measurementData)) {
                frameCount++;
                sumOfDistances = 0;
                for(int i=0; i<imageResolution; i++) {
                    sumOfDistances += measurementData.distance_mm[i];
                }

                theLogger.trace("Sum of mm: %d", sumOfDistances);

                if (abs(lastFrameSum - sumOfDistances) < 500) {
                    gotSimilarFrames = true;
                    theLogger.info("calibration done. it took %d frames.", frameCount);
                } else {
                    lastFrameSum = sumOfDistances;
                }

            }
        }

        if (frameCount > 500){
            theLogger.error("could not calibrate");
            gotSimilarFrames = true;
        }

        delay(5); // so we're not in a tight loop

    } while (!gotSimilarFrames);

    
    //if (myImager.getRangingData(&measurementData)) { //Read distance data into array
    
        // read out the measured data into an array
        for(int i = 0; i < 64; i++) {
        
            calibration[i] = measurementData.distance_mm[i];

            // adjust for calibration values being 0 or too long for measurement
            if( (calibration[i] == 0) || (calibration[i] > MAX_CALIBRATION) ) {
                calibration[i] = MAX_CALIBRATION;
            }

        }
#ifdef CONTINUOUS_DEBUG_DISPLAY
        moveTerminalCursorDown(20);
#endif
        Serial.println("Calibration data:");
        prettyPrint(calibration);
        Serial.println("End of calibration data\n");
   // }

   



}


/* ------------------------------ */
// process the measured data
void TPP_TOF::processMeasuredData(VL53L5CX_ResultsData measurementData, int32_t adjustedData[]) { 

    int statusCode = 0;
    int measuredData = 0;
    int32_t deltaDist = 0;

    for(int i = 0; i < imageResolution; i++) {
      
        // process the status code, only good data if status code is 5 or 9
        statusCode = measurementData.target_status[i];
        measuredData = measurementData.distance_mm[i];

        if( (statusCode != 5) && (statusCode != 9) && (statusCode != 6)) { // TOF measurement is bad
            
            adjustedData[i] = -1;

        } else if ( (measuredData == 0) || (measuredData > MAX_CALIBRATION) ) { 
         //data out of range
                
            adjustedData[i] = -2;  // indicate out of range data

        } else  {
            // data is good and in range, check if background
          
            // check new data against calibration value
            deltaDist = abs(measuredData - calibration[i]);

            if ((deltaDist <= NOISE_RANGE) || (measuredData > calibration[i]) ){ 
                    // zero out noise  
                
                    adjustedData[i] = -3; // data is background; ignore
            } 
            else { 
            
                    adjustedData[i] = (int16_t) measuredData;
            }

        }
        
    }
} 


/* ------------------------------ */
// returns number of adjacent zones that have valid distance data
int TPP_TOF::scoreZone(int location, int32_t dataArray[]){
    int score = 0;
    int locX, locY, loc;
    int locYInit = location/imageWidth;
    int locXInit = location % imageWidth;

    for(int yIndex = -1; yIndex <= 1; yIndex++) {
        for(int xIndex = -1; xIndex <= 1; xIndex++) {

            locX = locXInit+ xIndex;
            locY = locYInit + yIndex;

            if ((locX >= 0) && (locX < imageWidth) && (locY >= 0) && (locY < imageWidth)) {

                // determine the location in the dataArray of value to test for validity
                loc = (locY * imageWidth) + locX;

                if(dataArray[loc] > 0) { // valid value
                    score++;
                }
            }
        }
    }
    return score;
}

/* ------------------------------ */
// returns dist that is the average of surrounding valid zones
int TPP_TOF::avgdistZone(int location, int32_t distance[]){
    int totalDist = 0;
    int numZones = 0;
    int avgDist = 0;
    int locX, locY, loc;
    int locYInit = location/imageWidth;
    int locXInit = location % imageWidth;


    avgDist = distance[location];
    if (distance[location] > 0) { 
        for(int yIndex = -1; yIndex <= 1; yIndex++) {
            for(int xIndex = -1; xIndex <= 1; xIndex++) {

                locX = locXInit + xIndex;
                locY = locYInit + yIndex;

                if ((locX >= 0) && (locX < imageWidth) && (locY >= 0) && (locY < imageWidth)) {

                    // determine the location in the dataArray of value to test for validity
                    loc = (locY * imageWidth) + locX;
                    if (distance[loc] > 0 ) {
                        totalDist += distance[loc] ;
                        numZones++;
                    }
                }
            }
        }
        avgDist = totalDist / numZones;
    } 
    return avgDist;
}

/* ------------------------------ */
// function to decide if a zone is good enough for focus
bool TPP_TOF::validate(int score) {
    const int VALID_SCORE_MINIMUM = 3;
    
    if(score >= VALID_SCORE_MINIMUM) {
        return true;  
    } else {
        return false;
    }
}


/* ------------------------------ */
// function to move the terminal cursor back up to overwrite previous data printout
void TPP_TOF::moveTerminalCursorUp(int numlines) {
    String cursorUp = String("\033[") + String(numlines) + String("A");
    Serial.print(cursorUp);
    Serial.print("\r");
}

/* ------------------------------ */
// function to move the terminal cursor down to get past previous data printout - used on startup
void TPP_TOF::moveTerminalCursorDown(int numlines) {
    String cursorUp = String("\033[") + String(numlines) + String("B");
    Serial.print(cursorUp);
    Serial.print("\r");
}



// -------- getPOI ------------
// called anytime to have sensor read and interpret its zone data
// returns the current Point Of Interest
void TPP_TOF::getPOI(pointOfInterest *pPOI){

    pPOI->gotNewSensorData = false;
    pPOI->hasDetection = false;
    pPOI->x = -255;
    pPOI->y = -255;
    pPOI->distanceMM = -1;
    pPOI->detectedAtMS = -1;
    pPOI->calibrationDistMM = -1;

    int32_t adjustedData[imageResolution];

#ifdef CONTINUOUS_DEBUG_DISPLAY
    int32_t secondTable[imageResolution];   // second table to print out
    String secondTableTitle = ""; // will hold title of second table 

    // initialize second table
    for (int i = 0; i<imageResolution; i++) {
        secondTable[i] = 0;
    }
#endif
  
    //Poll sensor for new data.  Adjust if close to calibration value
    
    if (myImager.isDataReady() == true) {
    
        if (myImager.getRangingData(&measurementData)) { //Read distance data into ST driver array

            pPOI->gotNewSensorData = true;
       
            // initialize findings
            pPOI->distanceMM = MAX_CALIBRATION + 1; // start with the max allowed

            // process the measured data
            processMeasuredData(measurementData, adjustedData);
            
            // XXXX New criteria (v 0.8+ for establishing the smallest valid distance)
            //  Walk through the adjustedData array except for the edges.  For each possible
            //    smallest value found, check that surrounding values are valid.

            //
            // do not process the edges: x, y == 0 or x,y == 7  
            for (int y = 0; y < imageWidth; y++) {
                for (int x = 0; x < imageWidth; x++) {

                    int thisZone = y*imageWidth + x;

                    // Get the average distance of this zone
                    int avgDistThisZone = avgdistZone(thisZone, adjustedData);

                    int score = scoreZone(thisZone, adjustedData);
#ifdef CONTINUOUS_DEBUG_DISPLAY
                    secondTable[thisZone] = avgDistThisZone; 
#endif
                    // test for the smallest value that is a significant zone
                    //if( (avgDistThisZone > NOISE_RANGE) && (avgDistThisZone < smallestValue) &&
                    //    (validate(score) == true) ) {

                    if(        (adjustedData[thisZone] > 0)                       // less than 0 is to be ignored 
                            && (validate(score))                                 // has at least x adjacent zones with valid distances 
                            && (adjustedData[thisZone] < calibration[thisZone])   // closer than our calibration frame (this does not seem to matter)
                            && (adjustedData[thisZone] < pPOI->distanceMM)       // closer than current closest pPOI
                            && (avgDistThisZone > NOISE_RANGE)
                            ) {
                        // this pPOI will be the one closest to the sensor
                        pPOI->x  = x;
                        pPOI->y  = y;
                        pPOI->distanceMM = adjustedData[thisZone];
                        pPOI->detectedAtMS = millis();
                        pPOI->calibrationDistMM = calibration[thisZone];
                        pPOI->hasDetection = true; 
                        pPOI->surroundingHits =  score;
               
                    }
                }
            }



#ifdef CONTINUOUS_DEBUG_DISPLAY

            int linesPrinted = 0;
            linesPrinted = prettyPrint(adjustedData);

            // print out focus value found
            Serial.print("\nFocus on x = ");
            Serial.printf("%5ld", focusX);
            Serial.print(" y = ");
            Serial.printf("%5ld", focusY);
            Serial.print(" range = ");
            Serial.printf("%5ld", smallestValue);
            Serial.println();
            Serial.println();
            Serial.println();
            linesPrinted += 3;

            Serial.println("avgDistThisZone");
            linesPrinted += 1;
            linesPrinted += prettyPrint(secondTable);
            Serial.println();
            linesPrinted++;

            // overwrite the previous display
            moveTerminalCursorUp(linesPrinted+1);
#endif
            
        }
    }

}

// -------- getPOITemporalFiltered ------------
// called anytime to have sensor read and interpret its zone data
// returns the current Point Of Interest
// only if a detection has persisted for minTimeForDetectionMS
// this prevents spurious reports
void TPP_TOF::getPOITemporalFiltered(pointOfInterest *pPOI) {

    static bool waitingFirstDetection = true;
    static int sequentialFramesWithHit = 0;
    static int currentX = -1;
    static int currentY = -1;
    static int suppressedX = -1;
    static int suppressedY = -1;

    bool isPersistentDetection = false;

    // get new point of interest data
    getPOI(pPOI); 

    // EARLY RETURN <<<<<<<<<<<<<<<<<<<<<
    if ( ! pPOI->gotNewSensorData ) {
        // did not get new sensor data
        // return the pPOI that we just got as-is
        return;
    }

    if ( ! pPOI->hasDetection) {
        //theLogger.trace("no detection");
        waitingFirstDetection = true; 

    } else {

        if (waitingFirstDetection) {
            // we have a first detection
            //theLogger.trace("first detection (%4i, %4i) dist: %ld calib: %d deltaCalibr: %ld surrounding: %d", 
            //  pPOI->x, pPOI->y, pPOI->distanceMM, pPOI->calibrationDistMM, pPOI->distanceMM - pPOI->calibrationDistMM, pPOI->surroundingHits);
            waitingFirstDetection = false;
            sequentialFramesWithHit = 0;
            currentX = pPOI->x;
            currentY = pPOI->y;
            suppressedX = -1; // set up to log this one
            suppressedY = -1;
        } 
        
        // allow some spatial jitter
        int spatialDistX = 0 ;  //currentX - pPOI->x;
        int spatialDistY = 0 ;  //currentY - pPOI->y;
        if ((spatialDistX < 2) && (spatialDistY < 2)) {
            // same x,y so increment frame counter
            sequentialFramesWithHit++;
        } else {
            // x,y has changed so this becomes the first frame
            sequentialFramesWithHit = 1;
            currentX = pPOI->x;
            currentY = pPOI->y;
        }

        // do we have enough sequential frames to declare a hit?
        if (sequentialFramesWithHit >= FRAMES_FOR_GOOD_HIT) {
            // the frames filter has passed
            isPersistentDetection = true;
        }

        if (isPersistentDetection) {
            // we'll return the POI that we got

            // logging
            theLogger.trace("temporal filter returns point (%4i, %4i) dist: %d calib: %d deltaDist: %d frames: %d surrounding: %d", 
                pPOI->x, pPOI->y, pPOI->distanceMM, pPOI->calibrationDistMM, pPOI->distanceMM - pPOI->calibrationDistMM,
                 sequentialFramesWithHit, pPOI->surroundingHits);

        } else {
            // valid point, but not persistent so suppress this detection
            pPOI->hasDetection =  false; 

            // logging
            if((suppressedX != pPOI->x) && (suppressedY != pPOI->y) ) {
                // only report once for each x,y
                theLogger.trace("POI suppressed (%4i, %4i) dist: %d  calib: %d  delta: %d", 
                    pPOI->x,pPOI->y,pPOI->distanceMM,pPOI->calibrationDistMM,pPOI->distanceMM - pPOI->calibrationDistMM);
                suppressedX = pPOI->x;
                suppressedY = pPOI->y;
            }
        }
    } 
}


/* ------------------------------ */
// function to pretty print data to serial port
//   retuns number of lines printed
int TPP_TOF::prettyPrint(int32_t dataArray[]) {
    //The ST library returns the data transposed from zone mapping shown in datasheet
    //Pretty-print data with increasing y, decreasing x to reflect reality 

    int lines = 0;
    Serial.print("\t        ");
    for (int i = imageWidth-1; i >= 0; i--) {
        Serial.printf("%-5i",i);
    }
    Serial.println();
    lines++;
    for(int y = 0; y <= imageWidth * (imageWidth - 1) ; y += imageWidth)  {
        Serial.print("\t");
        Serial.printf("%-5i:  ", y/imageWidth);
        for (int x = imageWidth - 1 ; x >= 0 ; x--) {
            Serial.printf("%-5ld", dataArray[x + y]);
        }
        Serial.println();
        lines++;
    } 
    return lines;
}