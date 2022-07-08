/*
 Datalogger, takes data from CAN-bus and stores in a file on USB-memorystick.
 Setup for Arduino Mega.

Ver. 103
 - DiskCapacity not being checked unless requested, by CAN cmd.

Ver. 102
 - Filedate on new file creation.
 - *NOT IMPLEMENTED* two dimensional array for LogData, and only write every 10 sec.

Ver. 101
 - Filename detection.
 - Disk size check.
 - Added can-msg for setup info. *WIP*

Ver. 100
- Initial functions.

Datalogging 16 coloums writes etimated 48 bytes per second.
*/

// --------------------------------------------------
/*
VDRIVE2 Arduino Library Example Sketch
For use with an Arudino Mega or Due with the VDRIVE
attached to Serial1 and Serial0 going to the Serial
monitor in the Arduino environment.
*/
#include <VDRIVE2.h>  //Include the library
// --------------------------------------------------

/*
 CAN-bus module: http://henrysbench.capnfatz.com/henrys-bench/arduino-projects-tips-and-more/arduino-can-bus-module-pin-outs-and-schematics/
*/
#include <SPI.h>
#include "mcp_can.h"

// variables for CAN-bus communication.
long unsigned int rxID;
unsigned long rcvTime;
unsigned char flagRecv = 0;
unsigned char len = 0;
unsigned char buf[8];
//unsigned char buf_filename[8]; defined another place in code.
const int SPI_CS_PIN = 53;    // (Use pin 53 on Mega) - (Use pin ~D10 on UNO)
MCP_CAN CAN(SPI_CS_PIN);      // Set CS pin

long unsigned int rxID_cmd = 0x232;
long unsigned int rxID_data = 0x332;
long unsigned int rxID_setup = 0x432;
long unsigned int txID = 0x1B2;

// DATA LOG status variables.
boolean LogLoop = false;  //keep recieving data and writing to Disk.
boolean LogSampleComplete = false;

// DiskDrive status variables.
boolean DiskInserted = false;
boolean DiskInsertedLast = false;
boolean FileExcist = false;
boolean WriteNewHeader = false;
unsigned char DiskCap[6] = {0,0, 0, 0, 0, 0};
boolean DiskCapOK = false;
//

// Variable for ebabling the serial monitor readout.
int JumperInput;
boolean MonitorEna = true;
boolean MonitorEna_last = true;

// --------------------------------------------------
//Create an instance of the class
//CTS on pin 11, CTR on pin 10, 9600 baud
VDRIVE2 vdrive(9600, 11, 10);  

boolean feedback = true;  
/*Request feedback/confirmation through the Serial 
Monitor from the drive during read/write cycles 
(if FALSE the feedback data will not be displayed)
*/

byte datetime[] = {0x50, 0xC2, 0x6C, 0xC0};

//byte datetime[] = {B11100001, B10100001, B01110011, B00011001};
/*  Should be 1/1/2013
 Time Bytes - NOTE: to use VDRIVE2::OpenFile() 
 must be modified to take 4 values intstead of 2
   Second = bits 0-4   int 0-29   seconds/2
   Minute = bits 5-10  int 0-59
   Hours =  bits 11-15 int 0-23
 Date Bytes
   Days =   bits 0-4   int 1-31    
   Months = bits 5-8   int 1-12    
   Years =  bits 9-15  int 0-127   0=1980, 127=2107
*/

//Make a filename
String filename = "DEFAULT.CSV";  //Filname is set by recieved cmd message data.    
unsigned char buf_filename[8] = {0, 68, 69, 70, 65, 85, 76, 84}; //"DEFAULT" written in DEC ascii (ALL CAPS !!!). 

//Create an array to keep our data in
const int columns = 17;
word LogData[columns];          //Contains the data recieved.

int i = 0; //Intial data "seed"


// --------------------------------------------------

word DateStamp(unsigned char byte0,unsigned char byte1,unsigned char byte2,unsigned char byte3){
// Excel date format is 01-01-1900 [DD-MM-YYYY] -> Conversion is days since 01-01-1900.
// This is used for Log Data only.
  word TotalDays = word(byte3, byte2);
  unsigned char minute = byte0;  //byte0 = LSB 8bit
  unsigned char hour = byte1;    //byte1 = next LSB 8bit
  int year;
  int month;
  int day;
  return TotalDays;
}

word TimeStamp(unsigned char byte0,unsigned char byte1,unsigned char byte2,unsigned char byte3){
// Excel date format is 01-01-1900 [DD-MM-YYYY] -> Conversion is days since 01-01-1900.
// This is used for Log Data only.
  unsigned char minute = byte0;  //byte0 = LSB 8bit
  unsigned char hour = byte1;    //byte1 = next LSB 8bit
  word ZuluTime = (hour * 100) + minute;
  return ZuluTime;
}



// --------------------------------------------------
void LogginData(unsigned char DataArray[8]) {
//  Serial.print("Loggin active->");
  //word LogData[columns]; //Global 16bit unsigned variable.
  feedback = MonitorEna; // get subfunction feedback.

  switch(DataArray[0]){
    case 1:
      // 1: Absolute time [s]
      LogData[0] = DataArray[3]*256+DataArray[2];
      LogData[1] = DateStamp(DataArray[4],DataArray[5],DataArray[6],DataArray[7]);
      LogData[2] = TimeStamp(DataArray[4],DataArray[5],DataArray[6],DataArray[7]);
      break;
    case 2:
      // 2: Position [cm]
      LogData[3] = DataArray[3]*256+DataArray[2];
      break;
    case 3:
      // 3: Rotaryhead rev [rpm]
      LogData[4] = DataArray[3]*256+DataArray[2];
      break;
    case 4:
      // 4: Torque [Nm]
      LogData[5] = DataArray[3]*256+DataArray[2];
      break;
    case 5:
      // 5: PullDown []
      LogData[6] = DataArray[3]*256+DataArray[2];
      break;
    case 6:
      // 6: HoldBack []
      LogData[7] = DataArray[3]*256+DataArray[2];
      break;
    case 7:
      // 7: SystemPress []
      LogData[8] = DataArray[3]*256+DataArray[2];
      break;
    case 8:
      // 8: MudPress []
      LogData[9] = DataArray[3]*256+DataArray[2];
      break;
    case 9:
      // 9: MudFlow []
      LogData[10] = DataArray[3]*256+DataArray[2];
      break;
    case 10:
      // 10: AirPress []
      LogData[11] = DataArray[3]*256+DataArray[2];
      break;
    case 11:
      // 11: Weight []
      LogData[12] = DataArray[3]*256+DataArray[2];
      break;
    case 12:
      // 12: DrillSpeed []
      LogData[13] = DataArray[3]*256+DataArray[2];
      break;
    case 13:
      // 13: Depth []
      LogData[14] = DataArray[3]*256+DataArray[2];
      break;
    case 14:
      // 14: RodCount []
      LogData[15] = DataArray[3]*256+DataArray[2];
      break;
    case 15:
      // 15: ID-Tag [Nm]
      LogData[16] = DataArray[3]*256+DataArray[2];
      break;
    default:
      // do nothing
      break;
  }
  if(DataArray[1]==255){
    LogLoop = true;
  }
  else
  {
    LogLoop = false;
  }
  if(DataArray[0]==15){
  LogSampleComplete = true;}
  }
  
// --------------------------------------------------
void WriteSampleData() {
//  Serial.print("Writing logdata->");
  feedback = MonitorEna; // get subfunction feedback.

//String tmpData;
word tmpData[17] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};

    //Writes the data columns to the file (with 6 digit 
    //precision...this can be changed in the VDRIVE2.cpp file
    //NOTE: TestData MUST BE A DOUBLE PRECISION ARRAY 
    //with NUMBER OF ELEMENTS EQUAL TO COLUMNS! 
    Serial.print("#");
    delay(20);
           if(LogSampleComplete)
            {
              vdrive.OpenFile(filename, datetime, feedback);
              vdrive.WriteFileLogData(LogData, 17, feedback);
              vdrive.CloseFile(filename, feedback);
                Serial.print("Logtime:");
                Serial.print(LogData[0]);
                delay(20);
                LogSampleComplete = false;
            }
            Serial.println("[s]");
    //Reallocates the USB memory to include newly written data 
    //(i.e. permanently stores the newly written data)
  }

// --------------------------------------------------
void SendReadyToLog(unsigned char cmd_recevied) {
unsigned char stmp[8] = {cmd_recevied, 0xFF, 0xFF, 0x00, DiskCap[5], DiskCap[4], DiskCap[3], DiskCap[2]};
/*
 * byte0 = feedback requested command.
 * byte1 = DiskStatus
 * byte2 = FileStatus
 * ..
 * byte4-7 = DiskCapacity DiskCap[0] and DiskCap[1] are the least signifant bytes.
 */
  feedback = MonitorEna; // get subfunction feedback.
  DoesFileExist();
  delay(50);
  if(DiskInserted){
    DoesFileExist();
    delay(50);
  }
  
  if(DiskInserted){
    stmp[1]=0xFF; // Disk is OK.
    Serial.print(" Disk = OK ");
    if(FileExcist){
      stmp[2]=0xFF; // File is OK.
      Serial.println("/ File = OK ");
    }
    else
    {
      //stmp[2]=0xFF; // used for test!!
      stmp[2]=0; // File not found.
      Serial.println("/ File = N/A");
    }

    if((cmd_recevied == 1 || cmd_recevied == 2) && !FileExcist){
      stmp[3]=1;
      LogLoop = true;
      WriteNewHeader = true;
      stmp[2]=0xFF; // New File will be made.
      if(MonitorEna){
        Serial.println("Sending Ready To Start New Log message.");
      }
    }

    if((cmd_recevied == 1 || cmd_recevied == 2) && FileExcist){
      stmp[3]=2;
      LogLoop = true;
      WriteNewHeader = false;
      if(MonitorEna){
        Serial.println("Sending Ready To Resume Log message.");
      }
    }
  }

if(!DiskInserted){
    stmp[1]=0; // NoDisk
    stmp[2]=0; // NoFile
    if(MonitorEna){
      Serial.println("Disk Removed");
      Serial.println("Sending NOT yet ReadyToLog message.");
    }
    LogLoop = false;
  }

if(cmd_recevied == 64){
    // Recieved command to stop logging.
    stmp[3]=64;
    LogLoop = false;
    Serial.println("CMD: Stopping Log.");
    }

 //     CAN.sendMsgBuf(txID,1, 8, stmp);    
 // send data:  id = 0x00, standrad frame, data len = 8, stmp: data buf
    CAN.sendMsgBuf(txID, 0, 8, stmp);
    delay(100);
    if(MonitorEna){
      Serial.println(" *srtl* ");
    }
}
// --------------------------------------------------
void WriteLogHeader() {
  char CR = 0x0d; //Carriage return
  char LF = 0x0a; //LineFeed
  int col = 17;
  String LogHeadLine = String("Fil format data");
  String LogHeadings[col] = {"AbsTime;","Date;","Time;","Position;","RPM;","Torque;","PullDown;","HoldBack;","SystemPress;","MudPress;","MudFlow;","AirPress;","Weight;","DrillSpeed;","Depth;","Rodcount;","ID-TAG;"};
  feedback = MonitorEna; // get subfunction feedback.
  if(MonitorEna){
    // so whats new?
  }
  
  if(WriteNewHeader){
  // If the file does not exist the do write a new file.
  // Entering the header data for the logfile.
    Serial.println("Creating Headers...");
      //Open the file for writing
      vdrive.OpenFile(filename, datetime,  feedback);
      delay(20);
      //Write a header at the beginning of the file 
      LogHeadLine = "DRILLRIG LOGFIL ";
      vdrive.WriteFileStringln(LogHeadLine, feedback);
      delay(20);
      
      LogHeadLine = "Format of data field;DEC ";
      vdrive.WriteFileStringln(LogHeadLine, feedback);
      delay(20);
      
      vdrive.CloseFile(filename,  feedback);
      
      delay(50);
      for(int i = 0; i<col; i++)
        {
          vdrive.OpenFile(filename, datetime,  feedback);
          delay(20);
          if(i == col-1){
            vdrive.WriteFileStringln(LogHeadings[i], feedback); //last item, add newline
            if(MonitorEna){
              Serial.println(LogHeadings[i]);
            }
            delay(20);
          }
          else
          {
            vdrive.WriteFileString(LogHeadings[i], feedback);
            if(MonitorEna){
              Serial.print(LogHeadings[i]);
            }
          }
          delay(20);
          vdrive.CloseFile(filename,  feedback);
          delay(50);
        }

      //Reallocates the USB memory to include newly written data 
      //(i.e. permanently stores the newly written data)
      if(MonitorEna){
        Serial.println("Closing File...");
      }
    delay(50);
    WriteNewHeader = false; //The header has been written, now get on with it.
  }
}

// -------------------------------------------------------




// --------------------------------------------------

String Extract_filename(unsigned char arr[8]){
  String thefile = "";
  thefile.concat(char(arr[1]));
  thefile.concat(char(arr[2]));
  thefile.concat(char(arr[3]));
  thefile.concat(char(arr[4]));
  thefile.concat(char(arr[5]));
  thefile.concat(char(arr[6]));
  thefile.concat(char(arr[7]));
  thefile.concat(".CSV");
  return thefile;
}

// ----------------------DoesFileExist()----------------------------

void DoesFileExist(){
  // Check if File exist, and return FileStatus true.
  feedback = MonitorEna; // get subfunction feedback.
  if(MonitorEna){
//Serial.println("");
    Serial.println(" #dfe# ");
  }

  // When buffer charactervalue is 0, then use default filname.
  // Otherwise extract the "new" filename and use it.
  if(buf_filename[1]==0){
    if(MonitorEna){
      Serial.print("Not yet defined ");
    }
  }
  else
  {
    filename = Extract_filename(buf_filename);
  }
  if(MonitorEna){
    Serial.print(" -> ");
    Serial.print(filename);
  }
  delay(10);

  if(vdrive.CheckFile(filename, feedback)){
//  if(true == false){
    WriteNewHeader = false;
    FileExcist = true;
    if(MonitorEna){
      Serial.print(" [File found]");
    }
  }
  else
  {
    WriteNewHeader = true;
    FileExcist = false;  // Assume nothing
    if(MonitorEna){
      Serial.print(" [File not found]");
    }
  }
  if(MonitorEna){
//Serial.println("");
    Serial.println(" *dfe* ");
  }
}

// --------------------------------------------------

void copy_ARR(unsigned char* src, unsigned char* dst, int len) {
// Copies an array
    for (int i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

// --------------------------------------------------

void CheckDiskCapacity() {
  // Run the test of current disk capacity, and only proceed when value is avaviable.
  // USB-pendrive will flash for a minute or more, and USB-host will be unresponsive.
  // Expected reply from USB VDRIVE2
  // ?$00 $50 $FE $DE $01 $00 => ?1D EF E5 00 0? hex => 8.036.175.872? dec ˜ 8 Gb disk kapacitet.??
//  DiskCap[0] = 0; DiskCap[1] = 0; DiskCap[2] = 0; DiskCap[3] = 0; DiskCap[4] = 0; DiskCap[5] = 0;
  Serial.println("Checking Disk capacity, please wait.");

  String CapacityResult = String("");
  CapacityResult = vdrive.CheckSpace();
  // retry untill getting a result.
  for (int i = 0; i <= 255; i++) {
    Serial.print(":");
    unsigned int StringLength = CapacityResult.length();
    Serial.println(StringLength);
    if(StringLength > 1) {
      DiskCapOK = true;
      Serial.print(" > YUP ");
      break;
    }
      else
    {
      Serial.print(i);
      Serial.print(".");
      if (i >= 25) {
        // This has taken too long.
        DiskCapOK = false;
        Serial.println(" Error - could not check disk space or Diskcapacity too large.");
        break;
      }
    }
    // Wait for result.
    delay(10000);
    // Check if result is ready?
    CapacityResult = vdrive.CheckSpace();
  }
  Serial.print("-> ");
  if(DiskCapOK) {
  DiskCap[0] = makeitbyte(CapacityResult[1],CapacityResult[2]);
  DiskCap[1] = makeitbyte(CapacityResult[5],CapacityResult[6]);
  DiskCap[2] = makeitbyte(CapacityResult[9],CapacityResult[10]);
  DiskCap[3] = makeitbyte(CapacityResult[13],CapacityResult[14]);
  DiskCap[4] = makeitbyte(CapacityResult[18],CapacityResult[19]);
  DiskCap[5] = makeitbyte(CapacityResult[21],CapacityResult[22]);
  
  Serial.print(".");
  Serial.print(CapacityResult[1]);
  Serial.print(CapacityResult[2]);
//  Serial.print(CapacityResult[3]);
//  Serial.print(CapacityResult[4]); // $
  Serial.print(CapacityResult[5]);
  Serial.print(CapacityResult[6]);
//  Serial.print(CapacityResult[7]);
//  Serial.print(CapacityResult[8]); // $
  Serial.print(CapacityResult[9]);
  Serial.print(CapacityResult[10]);
//  Serial.print(CapacityResult[11]);
//  Serial.print(CapacityResult[12]); // $
  Serial.print(CapacityResult[13]);
  Serial.print(CapacityResult[14]);
//  Serial.print(CapacityResult[15]);
//  Serial.print(CapacityResult[16]); // $
  Serial.print(CapacityResult[17]);
  Serial.print(CapacityResult[18]);
//  Serial.print(CapacityResult[19]);
//  Serial.print(CapacityResult[20]); // $
  Serial.print(CapacityResult[21]);
  Serial.print(CapacityResult[22]);

  Serial.print("/");
  Serial.print(DiskCap[0]);
  Serial.print(".");
  Serial.print(DiskCap[1]);
  Serial.print(".");
  Serial.print(DiskCap[2]);
  Serial.print(".");  
  Serial.print(DiskCap[3]);
  Serial.print(".");
  Serial.print(DiskCap[4]);
  Serial.print(".");
  Serial.print(DiskCap[5]);
  Serial.print(" *X* ");
  }
}

// --------------------------------------------------****

byte makeitbyte(char textA, char textB) {
// 0xAB => A*16 + B 
  byte output = 0;
  byte a = 0;
  byte b = 0;

if((textA >= 48) && (textA <= 57)){
// between 0 and 9
  a = textA - 48;
}

if((textA >= 65) && (textA <= 70)){
// between A and F  
  a = textA - 55;
}

if((textB >= 48) && (textB <= 57)){
// between 0 and 9
  b = textB - 48;
}

if((textB >= 65) && (textB <= 70)){
// between A and F  
  b = textB - 55;
}
output = a*16 + b;

  return output;
}


// ########## - S E T U P - #########################################################

void setup() {
//configure pin 2 as an input and enable the internal pull-up resistor
  pinMode(13, INPUT_PULLUP);
  
//  Initialize CAN-bus interface module.
  {
    Serial.begin(9600);
    Serial.println("Datalogger version 1.03 - date: 12/06-2020");
    
    while (CAN_OK != CAN.begin(CAN_500KBPS))              // init can bus : baudrate = 250k !!! EVEN if it says "CAN_500KBPS"
    {
        Serial.println("CAN BUS Module Failed to Initialized");
        Serial.println("Retrying....");
        delay(200);
    }    
    Serial.println("CAN BUS Module Initialized!");

    /*
     * set mask, set both the mask to 0x3ff
     */
    CAN.init_Mask(0, 0, 0x3ff);                         // there are 2 mask in mcp2515, you need to set both of them
    CAN.init_Mask(1, 0, 0x3ff);


    /*
     * set filter, we can receive id from 0x232 ~ 0x
     */
    CAN.init_Filt(0, 0, 0x232);                         // there are 6 filter in mcp2515
    CAN.init_Filt(1, 0, 0x332);                         // there are 6 filter in mcp2515
    CAN.init_Filt(2, 0, 0x432);                          // there are 6 filter in mcp2515
    
    CAN.init_Filt(3, 0, 0x07);                          // there are 6 filter in mcp2515
    CAN.init_Filt(4, 0, 0x08);                          // there are 6 filter in mcp2515
    CAN.init_Filt(5, 0, 0x09);                          // there are 6 filter in mcp2515
    Serial.println("Filters initiated, 0x232, 0x332 and 0x432");    
  }
    delay(200);  //wait a bit
 
  //Intialize the Vdrive2
  vdrive.Initialize();
    Serial.println("VDRIVE Initialized..");
    delay(5000);  //wait a bit
    DiskInserted = vdrive.QueryDisk();
    DiskInsertedLast = DiskInserted;
    
    // Test..................................
    delay(500);  //wait a bit

    if(DiskInserted){
      //CheckDiskCapacity();
      DoesFileExist();
    }
}

// ########## - M A I N L O O P - #########################################################

void loop() {

// Read input 2, to determin if the serial monitor is on or off.
 JumperInput = digitalRead(13);
  if (JumperInput == HIGH) {
    MonitorEna = false;
  } else {
    MonitorEna = true;
    if(!MonitorEna_last && MonitorEna){
      Serial.println("Serial monitor active");

      delay(500);  //wait a bit
      DoesFileExist();
      delay(20);
      vdrive.CloseFile(filename,  feedback);
      delay(50);
    }
  }
  MonitorEna_last = MonitorEna;
  
  while (Serial1.available() > 0) {
    char a = Serial1.read();
    delay(5);
    Serial.print(a);
  }
//Query the drive to see if there is a disk in it
        DiskInserted = vdrive.QueryDisk();

// detect if disk has been inserted or Removed.
      if(DiskInserted && !DiskInsertedLast){
        if(MonitorEna){
          Serial.println("Disk Inserted - updating status");
        }    
        delay(500);
        SendReadyToLog(0);
        DiskInsertedLast = true;
      }

      if(!DiskInserted && DiskInsertedLast){
        if(MonitorEna){
          Serial.println("Disk Removed - updating status");
        }
        SendReadyToLog(0);
        DiskInsertedLast = false;        
      }
      
// If CAN-bus data is avaiable, do the stuff..
        while (CAN_MSGAVAIL == CAN.checkReceive())
        {
            // read data,  len: data length, buf: data buf
            CAN.readMsgBuf(&len, buf);    // read data,  len: data length, buf: data buf
            rxID = CAN.getCanId();
            if(rxID == rxID_cmd){
              if(MonitorEna){
                Serial.print(buf[0]);
                Serial.print("=> ");
              }
              switch(buf[0]){
                case 1:
                  // Create LogFile and wait for LogData.
                  copy_ARR(buf, buf_filename, 8);
                  filename = Extract_filename(buf_filename);
                  if(MonitorEna){
                  Serial.println("CMD: Create LogFile and wait for LogData");
                  Serial.print("filename:");
                  Serial.println(filename);
                  }
                  LogLoop = true;
                  if(DiskInserted){
                    WriteLogHeader();
                  }
                  SendReadyToLog(buf[0]);
                  break;
                case 2:
                  // Resume LogFile and wait for LogData.
                  copy_ARR(buf, buf_filename, 8);
                  filename = Extract_filename(buf_filename);
                  if(MonitorEna){
                    Serial.println("CMD: Resume LogFile and wait for LogData.");
                    Serial.print("filename:");
                    Serial.println(filename);
                  }
                  LogLoop = true;                  
                  if(DiskInserted){
                    WriteLogHeader();
                  }
                  SendReadyToLog(buf[0]);
                  break;
                case 3:
                  // Check if File exist, and return FileStatus true.
                  if(MonitorEna){
                    Serial.print("CMD: Check if File exist, and return FileStatus true.");
                    Serial.print("-> filename:");
                  }
                  copy_ARR(buf, buf_filename, 8);
                  DoesFileExist();
                  LogLoop = false;
                  SendReadyToLog(3);
                  break;
                default:
                  // do nothing
                  Serial.print("< o >");
                  break; 
              }

            if(buf[0] == 0x10){
                  // (CMD=16) View.
                  Serial.println("CMD: ViewStatus.");
                  delay(10);                  
                  vdrive.ViewStatus();
              }
                  
            if(buf[0] == 0x20){
                  // (CMD=32) Check DiskStatus, and return DiskStatus true + DiskSpace.
                  delay(10);                  
                  String filesize = vdrive.CheckSpace();
                  delay(100);
                  CheckDiskCapacity();
                  SendReadyToLog(32); //Returns return DiskStatus + DiskSpace.
                  if(MonitorEna){
                    Serial.println("CMD: Check DiskStatus, and return DiskStatus true + DiskSpace.");
                    Serial.println(filesize);
                    }
                  LogLoop = false;
              }
                                
            if(buf[0] == 0x40){
                  // (CMD=64) Close file and StopLog.
                  if(MonitorEna){
                    Serial.println("CMD: Close file and StopLog.");
                  }
                  delay(10);
                  vdrive.CloseFile(filename, feedback);
                  SendReadyToLog(64); //Returns the STOP recieved code.
                  LogLoop = false;
              }
            }

            if(rxID == rxID_data){
              if(MonitorEna){
                Serial.print(buf[0]);
                Serial.print(":");
                Serial.print(LogLoop, DEC);
                Serial.print("/");
                Serial.print(buf[3]*256+buf[2]);
                Serial.print(" ->");
                if(buf[0]== 15) Serial.println("");
                }
              }
            
            if(LogLoop && rxID == rxID_data && DiskInserted){
              LogginData(buf);
              if(LogSampleComplete){
                WriteSampleData();
                }
              }

            if(rxID == rxID_setup){
              // Setup info;
              switch(buf[0]){
                case 1:
                  // Set LogFile Date and Time.
                  datetime[0] = buf[2];
                  datetime[1] = buf[1];
                  datetime[2] = buf[4];
                  datetime[3] = buf[3];
                  /*  Should be 1/1/2013
                    Time Bytes - NOTE: to use VDRIVE2::OpenFile() 
                    must be modified to take 4 values intstead of 2
                    Second = bits 0-4   int 0-29   seconds/2
                    Minute = bits 5-10  int 0-59
                    Hours =  bits 11-15 int 0-23
                   
                    Date Bytes
                    Days =   bits 0-4   int 1-31    
                    Months = bits 5-8   int 1-12    
                    Years =  bits 9-15  int 0-127   0=1980, 127=2107
                   */
                  if(MonitorEna){
                      Serial.print("Setup: Set new Date and Time, buf[1] A:");
                      Serial.print(datetime[0],BIN);
                      Serial.print("/");
                      Serial.print(datetime[0],HEX);
                      Serial.print(",buf[2] B:");
                      Serial.print(datetime[1],BIN);
                      Serial.print("/");
                      Serial.print(datetime[1],HEX);
                      Serial.print(", buf[3] C:");
                      Serial.print(datetime[3],BIN);
                      Serial.print("/");
                      Serial.print(datetime[3],HEX);
                      Serial.print(", buf[4] D:");
                      Serial.print(datetime[2],BIN);
                      Serial.print("/");
                      Serial.println(datetime[2],HEX);
                    }
                  break;
                default:
                  // do nothing
                  if(MonitorEna){
                      Serial.println("Setup: Recieved but did nothing.");
                    }
                  break; 
              }
            }
        }
}

// END FILE
