/*
  Datalogger, takes data from CAN-bus and stores in a file.
  Setup for Arduino Mega, with AirLift ESP32 (Wifi/SD) shield,
    GPS module and CAN-bus module.

Datalogging 16 coloums writes etimated 48 bytes per second.
*/

#include <SPI.h>
#include <SD.h>
#include <WiFiNINA.h>
#include <EEPROM.h>

#include "CAN_code.h"
#include "Net_code.h"
#include "net_definitions.h"
#include "SD_code.h"
#include "GPS_code.h"

#include "mcp_can.h"

// **** ---------------------------------------------------------- ****
// Debug variables.

boolean debug = false;  // true = more messages

// Set GPSECHO to 'false' to turn off echoing the GPS data to the Serial console
// Set to 'true' if you want to debug and listen to the raw GPS sentences
#define GPSECHO false

boolean feedback = true;  
/*Request feedback/confirmation through the Serial 
Monitor from the drive during read/write cycles 
(if FALSE the feedback data will not be displayed)
*/

// **** ---------------------------------------------------------- ****

// comment out next line to write to SD from FTP server
#define FTPWRITE

File myFile;
File root;

char filename_eventlog[15] = "EventLog.txt";
char filename_config[15] = "config.ini";
String filepath_logdata = "DATA'\'";
char filename_logdata[15] = "default.csv";
char filename[15] = "test.txt";
String text;

// **** ---------------------------------------------------------- ****

#include <Adafruit_GPS.h>

// what's the name of the hardware serial port?
#define GPSSerial Serial1

// Connect to the GPS on the hardware port
Adafruit_GPS GPS(&GPSSerial);

// Setting the timer function.
uint32_t timer = millis();


// **** ---------------------------------------------------------- ****

int pingResult;
bool pingRequest = true;

// Configure the pins used for the ESP32 connection
#define SPIWIFI       SPI  // The SPI port
#define SPIWIFI_SS    10   // Chip select pin
#define ESP32_RESETN  5    // Reset pin
#define SPIWIFI_ACK   7    // a.k.a BUSY or READY pin
#define ESP32_GPIO0   6


int keyIndex = 0;               // your network key Index number (needed only for WEP)

  char Net_ID[20] = "undefined";  // your network SSID (name)
  char Net_PW[20] = "undefined";  // your network password (use for WPA, or use as key for WEP)
//  char FTP_IP[20] = "undefined";
//  char FTP_user[20] = "undefined";
//  char FTP_PW[20] = "undefined";

  String FTP_IP = "undefined";
  String FTP_user = "undefined";
  String FTP_PW = "undefined";

// **** ---------------------------------------------------------- ****

int status = WL_IDLE_STATUS;
// if you don't want to use DNS (and reduce your sketch size)
// use the numeric IP instead of the name for the server:
//IPAddress server(74,125,232,128);  // numeric IP for Google (no DNS)

// change to your server
IPAddress server( 192, 168, 1, 51 );

// Initialize the Wifi client library
WiFiClient client;
WiFiClient dclient;

char outBuf[128];
char outCount;

// **** ---------------------------------------------------------- ****
// SPIFFS file handle
File fh;


// **** ----------------- CAN-bus variables ---------------------- ****

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

// **** ---------------------------------------------------------- ****

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

// **** ---------------------------------------------------------- ****



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
// String filename = "DEFAULT.CSV";  //Filname is set by recieved cmd message data.    !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! change ?
unsigned char buf_filename[8] = {0, 68, 69, 70, 65, 85, 76, 84}; //"DEFAULT" written in DEC ascii (ALL CAPS !!!). 

//Create an array to keep our data in
const int columns = 17;
word LogData[columns];          //Contains the data recieved.

int i = 0; //Intial data "seed"

// **** ---------------------------------------------------------- ****

// ---------------- FUNCTION - efail() --------------------------------
// FTP detecting a failed connection.

void efail() {
  byte thisByte = 0;

  client.println(F("QUIT"));

  while (!client.available()) delay(1);

  while (client.available()) {
    thisByte = client.read();
    Serial.write(thisByte);
  }

  client.stop();
  Serial.println(F("Command disconnected"));
  fh.close();
  Serial.println(F("SD closed"));
}  // efail

// **** ---------------------------------------------------------- ****

// ---------------- FUNCTION - eRcv() ---------------------------------
// FTP recieveing a response.

byte eRcv() {
  byte respCode;
  byte thisByte;

  while (!client.available()) delay(1);

  respCode = client.peek();

  outCount = 0;

  while (client.available()) {
    thisByte = client.read();
    Serial.write(thisByte);

    if (outCount < 127) {
      outBuf[outCount] = thisByte;
      outCount++;
      outBuf[outCount] = 0;
    }
  }

  if (respCode >= '4') {
    efail();
    return 0;
  }
  return 1;
}  // eRcv()

// **** ---------------------------------------------------------- ****

// ---------------- FUNCTION - doFTP() --------------------------------
// FTP connection handling.

byte doFTP(String filename)
{
  
#ifdef FTPWRITE
  fh = SD.open(filename,FILE_READ);
#else
  SD.remove(filename);
  fh = SD.open(filename,FILE_WRITE);
#endif


  if(!fh)
  {
    Serial.println(F("SD open fail"));
    return 0;    
  }

#ifndef FTPWRITE  
  if(!fh.seek(0))
  {
    Serial.println(F("Rewind fail"));
    fh.close();
    return 0;    
  }
#endif

  Serial.println(F("SD opened"));

  if (client.connect(server,21)) {
    Serial.println(F("Command connected"));
  }
  else {
    fh.close();
    Serial.println(F("Command connection failed"));
    return 0;
  }

  if(!eRcv()) return 0;

// enter FTP Username
//  String tmpStr = "USER " + FTP_user;
 client.println(F("USER TesterUno"));
//   client.println(F("USER " + FTP_user + ""));
   
  if(!eRcv()) return 0;
// enter FTP Password
  // tmpStr = "PASS " + FTP_PW;
 client.println(F("PASS 12345678"));
//   client.println(F("PASS "+FTP_PW+""));
   
  if(!eRcv()) return 0;

  client.println(F("SYST"));

  if(!eRcv()) return 0;

  client.println(F("Type I"));

  if(!eRcv()) return 0;

  client.println(F("PASV"));

  if(!eRcv()) return 0;

  char *tStr = strtok(outBuf,"(,");
  int array_pasv[6];
  for ( int i = 0; i < 6; i++) {
    tStr = strtok(NULL,"(,");
    array_pasv[i] = atoi(tStr);
    if(tStr == NULL)
    {
      Serial.println(F("Bad PASV Answer"));    

    }
  }

unsigned int hiPort,loPort;

  hiPort = array_pasv[4] << 8;
  loPort = array_pasv[5] & 255;

  Serial.print(F("Data port: "));
  hiPort = hiPort | loPort;
  Serial.println(hiPort);

  if (dclient.connect(server,hiPort)) {
    Serial.println(F("Data connected"));
  }
  else {
    Serial.println(F("Data connection failed"));
    client.stop();
    fh.close();
    return 0;
  }

#ifdef FTPWRITE
  client.print(F("STOR "));
  client.println(filename);
#else
  client.print(F("RETR "));
  client.println(filename);
#endif

  if(!eRcv())
  {
    dclient.stop();
    return 0;
  }

#ifdef FTPWRITE
  Serial.println(F("Writing"));

  byte clientBuf[64];
  int clientCount = 0;

  while(fh.available())
  {
    clientBuf[clientCount] = fh.read();
    clientCount++;

    if(clientCount > 63)
    {
      dclient.write(clientBuf,64);
      clientCount = 0;
    }
  }

  if(clientCount > 0) dclient.write(clientBuf,clientCount);

#else
  while(dclient.connected())
  {
    while(dclient.available())
    {
      char c = dclient.read();
      fh.write(c);      
      Serial.write(c);
    }
  }
#endif

  dclient.stop();
  Serial.println(F("Data disconnected"));

  if(!eRcv()) return 0;

  client.println(F("QUIT"));

  if(!eRcv()) return 0;

  client.stop();
  Serial.println(F("Command disconnected"));

  fh.close();
  Serial.println(F("SD closed"));
  return 1;
}
// doFTP() ### end ###
// **** ---------------------------------------------------------- ****


// ---------------- FUNCTION - DateStamp() ----------------------------

word DateStamp(unsigned char byte0,unsigned char byte1,unsigned char byte2,unsigned char byte3){
// Excel date format is 01-01-1900 [DD-MM-YYYY] -> Conversion is days since 01-01-1900.
// This is called the Julian date format.
// This is used for Log Data only.
  word TotalDays = word(byte3, byte2);
  unsigned char minute = byte0;  //byte0 = LSB 8bit
  unsigned char hour = byte1;    //byte1 = next LSB 8bit
  int year;
  int month;
  int day;
// Convertion

  byte Monthdays[12] = {31,0,31,30,31,30,31,31,30,31,30,31}; // days in each month.
  int Days;

  // is this year a shooting year?
  if ((((year*100)/4) MOD 100) == 0){
    Monthdays[1] = 29; // skud år
  }
  else
  {
    Monthdays[1] = 28; // normalt år  
  }

  Days = day; // counting days in the current year.
  // is this month January
  if (month == 1){
    Days = day;
  }
  else
  {
    for (int i = 0; i < month; i++) {
      Days = Days + Monthdays[i]; //Adding the days from each month. 
    }
  }


TotalDays = (((((year-1900)*100)/4)*1461)/100)+Days;

// Unsigned 32 bit "word" result.  
  return TotalDays;
}
// **** ---------------------------------------------------------- ****


// ---------------- FUNCTION - TimeStamp() ----------------------------

word TimeStamp(unsigned char byte0,unsigned char byte1,unsigned char byte2,unsigned char byte3){
// Excel date format is 01-01-1900 [DD-MM-YYYY] -> Conversion is days since 01-01-1900.
// This is used for Log Data only.
  unsigned char minute = byte0;  //byte0 = LSB 8bit
  unsigned char hour = byte1;    //byte1 = next LSB 8bit
  word ZuluTime = (hour * 100) + minute;
  return ZuluTime;
}
// **** ---------------------------------------------------------- ****


// ---------------- FUNCTION - LogginData() ---------------------------
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
// **** ---------------------------------------------------------- ****

// ---------------- FUNCTION - WriteSampleData() ----------------------
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
  /*
              vdrive.OpenFile(filename, datetime, feedback);
              vdrive.WriteFileLogData(LogData, 17, feedback);
              vdrive.CloseFile(filename, feedback);
 */
                Serial.print("Logtime:");
                Serial.print(LogData[0]);
                delay(20);
                LogSampleComplete = false;
            }
            Serial.println("[s]");
    //Reallocates the USB memory to include newly written data 
    //(i.e. permanently stores the newly written data)
  }
// **** ---------------------------------------------------------- ****

// ---------------- FUNCTION - SendReadyToLog() -----------------------
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
  //                  DoesFileExist();                                                                              !!! insert variables.
  delay(50);
  if(DiskInserted){
    //                  DoesFileExist();                                                                              !!! insert variables.
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
// **** ---------------------------------------------------------- ****

// ---------------- FUNCTION - WriteLogHeader() -----------------------

void WriteLogHeader() {
// TESTING IN PROGRESS !!!
  /*
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
  */
}

// **** ---------------------------------------------------------- ****

// ---------------- FUNCTION - Extract_filename() ---------------------
// 

void Extract_filename(char extractedname[], unsigned char arr[8]){

  String thefile = "";
  thefile.concat(char(arr[1]));
  thefile.concat(char(arr[2]));
  thefile.concat(char(arr[3]));
  thefile.concat(char(arr[4]));
  thefile.concat(char(arr[5]));
  thefile.concat(char(arr[6]));
  thefile.concat(char(arr[7]));
  thefile.concat(".CSV");
  
   for(int i = 0; i < 15; i++){
     extractedname [i] = thefile[i];
   }
}

// **** ---------------------------------------------------------- ****



// ---------------- FUNCTION - copy_ARR() -----------------------------
// Copies an array with a length of 'len' from 'src' to 'dst'.

void copy_ARR(unsigned char* src, unsigned char* dst, int len) {
// Copies an array
    for (int i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}
// **** ---------------------------------------------------------- ****

// ---------------- FUNCTION - CheckDiskCapacity() --------------------
void CheckDiskCapacity() {
  // Run the test of current disk capacity, and only proceed when value is avaviable.
  // USB-pendrive will flash for a minute or more, and USB-host will be unresponsive.
  // Expected reply from USB VDRIVE2
  // ?$00 $50 $FE $DE $01 $00 => ?1D EF E5 00 0? hex => 8.036.175.872? dec ˜ 8 Gb disk kapacitet.??
//  DiskCap[0] = 0; DiskCap[1] = 0; DiskCap[2] = 0; DiskCap[3] = 0; DiskCap[4] = 0; DiskCap[5] = 0;
  Serial.println("Checking Disk capacity, please wait.");

// TESTING IN PROGRESS !!!

/*
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
  */
}
// **** ---------------------------------------------------------- ****

// ---------------- FUNCTION - makeitbyte() ---------------------------
// Convert two characters to a byte value.

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
// **** ---------------------------------------------------------- ****

// ---------------- FUNCTION - PauseCountdown() -----------------------
// Pause time which prints out a '.' every second.

void PauseCountdown(int n_tm){
    while(n_tm > 0){
      delay(1000); // 1 second.
      Serial.print(".");
      n_tm--;
    }
      Serial.println("");
  }

// ---------------- ----------------------- ------------------------------


/*
 * #######################################################################
 * ########## - S E T U P - ### - S E T U P - ### - S E T U P - ##########
 * #######################################################################
*/

void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  delay(500);
  PrintVersionOut();
  Serial.println(filepath_logdata);
// ------------------------------------------------------------------------------------
  Serial.println(F("Initializing GPS"));

  // 9600 NMEA is the default baud rate for Adafruit MTK GPS's- some use 4800
  GPS.begin(9600);
  //GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCONLY);
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_100_MILLIHERTZ); // 10 second update time
  // For the parsing code to work nicely and have time to sort thru the data,
  // and print it out we don't suggest using anything higher than 1 Hz

  // Request updates on antenna status, comment out to keep quiet
  GPS.sendCommand(PGCMD_ANTENNA);

  delay(1000);

  // Ask for firmware version
  GPSSerial.println(PMTK_Q_RELEASE);
  
  PrintLineOut();  //Screen line printed.
// ------------------------------------------------------------------------------------
    //Initialize SD Card......................
  Serial.print(F("Initializing SD card..."));

  if (!SD.begin(4)) {
    Serial.println(F("initialization failed!"));
    //while (1);
  }
  else
  {
    Serial.println(F("initialization done."));
  }
  
  root = SD.open("/");
//  printDirectory(root, 0);


  if (DoesFileExist(filename_config, "rootdir", true)) {
    // open the file. note that only one file can be open at a time,
    // so you have to close this one before opening another.
    myFile = SD.open(filename_config);
    if (myFile) {
      Serial.print(F("Opening file: "));
      Serial.print(filename_config);
      Serial.println(F(" ->"));
    
    // read from the file until there's nothing else in it:
    while (myFile.available()) {
      //Serial.write(myFile.read());
      text = text + char(myFile.read());
    }
    // close the file:
    myFile.close();
  } else {
    // if the file didn't open, print an error:
    Serial.print(F("error opening "));
    Serial.println(filename_config);
  }
}
 

  PrintLineOut();  //Screen line printed.
// ------------------------------------------------------------------------------------ 
  //Initialize AirLift ESP32 Wifi module.
  // check for the WiFi module:
  WiFi.setPins(SPIWIFI_SS, SPIWIFI_ACK, ESP32_RESETN, ESP32_GPIO0, &SPIWIFI);
  while (WiFi.status() == WL_NO_MODULE) {
    Serial.println(F("Communication with WiFi module failed!"));
    // don't continue
    delay(1000);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < "1.0.0") {
    Serial.println(F("Please upgrade the firmware"));
  }
  Serial.print(F("Found ESP32 firmware version:"));
  Serial.println(fv);
  PrintLineOut();  //Screen line printed.
 // ------------------------------------------------------------------------------------
 // Init. user selection
  
  Serial.println(F("Options:"));
  Serial.println(F("1 - Readout SD filelist"));
  Serial.println(F("2 - Display Config file"));  
  Serial.println(F("3 - Scan for Wifi networks"));
  Serial.println(F("4 - Change Wifi config"));
  Serial.println(F("5 - Change FTP config"));
  Serial.println(F("6 - "));
  Serial.println(F("7 - Copy Logfile"));
  Serial.println(F("8 - Display Logfile"));
  Serial.println(F("9 - Delete Logfile"));
  Serial.println(F("or wait 5 seconds to continue."));
  
int wait = 50;
byte UserChoice = 0;

while (wait > 0){
 // read serial input from port 0, and perform some command.
  Serial.print("."); 
  if (Serial.available()) {
    char inChar = Serial.read();
    if (inChar == '1') {
      UserChoice = 1; 
      wait = 0;
    }
    if (inChar == '2') {
      UserChoice = 2; 
      wait = 0;
    }
    if (inChar == '3') {
      UserChoice = 3; 
      wait = 0;
    }
    if (inChar == '4') {
      UserChoice = 4; 
      wait = 0;
    }
    if (inChar == '5') {
      UserChoice = 5; 
      wait = 0;
    }
    if (inChar == '6') {
      UserChoice = 6; 
      wait = 0;
    }
    if (inChar == '7') {
      UserChoice = 7; 
      wait = 0;
    }
    if (inChar == '8') {
      UserChoice = 8; 
      wait = 0;
    }
    if (inChar == '9') {
      UserChoice = 9; 
      wait = 0;
    }  
  }
  
  delay(100);
  wait --;
  if (wait == 0){
    Serial.println("");
    Serial.println(F("Timeout - continuing"));
    WriteToFile(filename_eventlog,"ok","Okt-2020");
//    WriteToFile(filename_eventlog,"ok",stampDateTime(now));
    }
}
Serial.println("");

  switch (UserChoice){
    case 1:
      // 1 List the SD-card files.
      Serial.println(F("xxxxxxxxxxxxxx SD-card files xxxxxxxxxxxxxxxx"));
      printDirectory(root, 0);
      Serial.println(F("xxxxxxxxxxxxxxx xxxxxxxxxxx xxxxxxxxxxxxxxxxx"));
      break;
    case 2:
      // 2 Display config file.
      Serial.println(F("xxxxxxxxxxxxxx Config readout xxxxxxxxxxxxxxx"));
      Serial.print(text);
      Serial.println(F("xxxxxxxxxxxxxxx End of file xxxxxxxxxxxxxxxxx"));
      break;
    case 3:
      // 3 Scan for wifi.
      Serial.println(F("xxxxxxxxxxxxxxx xxxxxxxxxxx xxxxxxxxxxxxxxxxx"));
      listNetworks();
      Serial.println(F("xxxxxxxxxxxxxxx xxxxxxxxxxx xxxxxxxxxxxxxxxxx"));
      break;
    case 4:
      // 4 Change Wifi login
      Serial.println("-");
      DeleteFile(filename_config);
      WriteConfig(filename_config,FTP_IP);
      break;
    case 5:
      // 5 Change FTP config
      Serial.println("5-Config");
      break;
    case 7:
      // 7 Copy Logfile backup
      Serial.println(F("7-Copy Logfile backup"));
//      Copy_SD_File(filename_eventlog,"LOG_back.txt");              // NOT WORKING or Needed yet.
      break;
    case 8:
      // 8 Display the LogFile
      Serial.println(F("xxxxxxxxxxxxxx Config readout xxxxxxxxxxxxxxx"));
//      Serial.println(filename_eventlog);
      Serial.print(ReadoutFile(filename_eventlog));
      Serial.println(F("xxxxxxxxxxxxxxx End of file xxxxxxxxxxxxxxxxx"));
      break;
    case 9:
      // 9 Delete the LogFile
      DeleteFile(filename_eventlog);
      Serial.print(F("6-DEL: "));
      Serial.println(filename_eventlog);
      break;      
    default:
      //
      Serial.println(F("No option selected, proceeding."));
      break;
  }
  
  PrintLineOut();  //Screen line printed.
// ------------------------------------------------------------------------------------
// Extracting Configuration data from configfile.

  String tmpStr;
  int i=0;

  tmpStr = ExtractConfig(text, "SSID");
  i=0;
  while (tmpStr[i] != '\0' ){
    Net_ID[i] = tmpStr[i];
    i++;
    Net_ID[i] = '\0';
  }

  tmpStr = ExtractConfig(text, "Wifi_PW");
  i=0;
  while (tmpStr[i] != '\0' ){
    Net_PW[i] = tmpStr[i];
    i++;
    Net_PW[i] = '\0';
  }
  
  FTP_IP = ExtractConfig(text, "FTP_server_IP");
  FTP_user = ExtractConfig(text, "FTP_server_user");
  FTP_PW = ExtractConfig(text, "FTP_server_PW");

//  Serial.println("ID: " + Net_ID + " / PW: " + Net_PW);
  Serial.print("SSID: ");
  Serial.println(Net_ID);

  Serial.println("FTP IP: " + FTP_IP);
  Serial.println("User: " + FTP_user + " / PW: " + FTP_PW);

  PrintLineOut();  //Screen line printed.
// ------------------------------------------------------------------------------------
  // attempt to connect to Wifi network:
  Serial.println(F("Attempting to connect to SSID: "));

  // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
  do {
    status = WiFi.begin(Net_ID, Net_PW);
    //status = WiFi.begin(ssid, pass);   
    delay(100);     // wait until connection is ready!
  } while (status != WL_CONNECTED);

  Serial.println(F("Connected to wifi"));
  WriteToFile(filename_eventlog,"[Connected to wifi]","Just now");
  printWifiStatus();
  
  PrintLineOut();  //Screen line printed.
// ------------------------------------------------------------------------------------
  
//  Initialize CAN-bus interface module.
  {
    while (CAN_OK != CAN.begin(CAN_500KBPS))              // init can bus : baudrate = 250k !!! EVEN if it says "CAN_500KBPS"
    {
        Serial.println(F("CAN BUS Module Failed to Initialized"));
        Serial.println(F("Retrying...."));
        delay(200);
    }    
    Serial.println(F("CAN BUS Module Initialized!"));

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
    Serial.println(F("Filters initiated, 0x232, 0x332 and 0x432"));    
  }
    delay(200);  //wait a bit
 
    DiskInserted = true;
    DiskInsertedLast = DiskInserted;
    
    // Test..................................
    delay(500);  //wait a bit

    if(DiskInserted){
      //CheckDiskCapacity();
//                  DoesFileExist();                                                                              !!! insert variables.
    }
    
    PrintLineOut();  //Screen line printed.
// ------------------------------------------------------------------------------------
}

// ########## - M A I N L O O P - #########################################################

void loop() {
  char keypress = '0'; //used in mainloop, for user input.

  if (Serial.available()) {
    keypress = Serial.read();
    if (keypress != '0') {
      Serial.print(F("keypress: "));
      Serial.println(keypress);
    }
  }
/* 
  // Test..................................

  Tjek Log arkiv
  - Send data til FTP
    - oprydning i arkiv.
  Tjek CAN cmd
  - Ny LogFil
  - Start LOG
  
  
  
  
  
  // Test..................................
*/
  
// Read input 2, to determin if the serial monitor is on or off.
 //JumperInput = digitalRead(13);
 JumperInput = LOW;
 if (JumperInput == HIGH) {
    MonitorEna = false;
  } else {
    MonitorEna = true;
    if(!MonitorEna_last && MonitorEna){
      Serial.println(F("Serial monitor active"));

      delay(500);  //wait a bit
//                  DoesFileExist();                                                                              !!! insert variables.
      delay(20);
//      vdrive.CloseFile(filename,  feedback);
      delay(50);
    }
  }
  MonitorEna_last = MonitorEna;

//Query the drive to see if there is a disk in it
        //DiskInserted = vdrive.QueryDisk();
        DiskInserted = true;

// detect if disk has been inserted or Removed.
      if(DiskInserted && !DiskInsertedLast){
        if(MonitorEna){
          Serial.println(F("Disk Inserted - updating status"));
        }    
        delay(500);
        SendReadyToLog(0);
        DiskInsertedLast = true;
      }

      if(!DiskInserted && DiskInsertedLast){
        if(MonitorEna){
          Serial.println(F("Disk Removed - updating status"));
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
//                  filename = Extract_filename(buf_filename);
                  Extract_filename(filename, buf_filename);
                  if(MonitorEna){
                  Serial.println(F("CMD: Create LogFile and wait for LogData"));
                  Serial.print(F("filename:"));
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
//                  filename = Extract_filename(buf_filename);
                  Extract_filename(filename, buf_filename);
                  if(MonitorEna){
                    Serial.println(F("CMD: Resume LogFile and wait for LogData."));
                    Serial.print(F("filename:"));
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
                    Serial.print(F("CMD: Check if File exist, and return FileStatus true."));
                    Serial.print(F("-> filename:"));
                  }
                  copy_ARR(buf, buf_filename, 8);
//                  DoesFileExist();                                                                              !!! insert variables.
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
                  Serial.println(F("CMD: ViewStatus."));
                  delay(10);                  
                  //vdrive.ViewStatus();  !!!!!!!!!!!!!!!!!!!! udskriv Log ?? måske
              }
                  
            if(buf[0] == 0x20){
                  // (CMD=32) Check DiskStatus, and return DiskStatus true + DiskSpace.
                  delay(10);                  
                  //String filesize = vdrive.CheckSpace();
                  String filesize = "123";
                  delay(100);
                  CheckDiskCapacity();
                  SendReadyToLog(32); //Returns return DiskStatus + DiskSpace.
                  if(MonitorEna){
                    Serial.println(F("CMD: Check DiskStatus, and return DiskStatus true + DiskSpace."));
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
//                  vdrive.CloseFile(filename, feedback); !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Omskriv !!
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
                      Serial.println(F("Setup: Recieved but did nothing."));
                    }
                  break; 
              }
            }
        }
//************ MAINLOOP - WIFI **********************************************************

//************ MAINLOOP - FTP **********************************************************

  if(keypress == 'f') {
    if(doFTP("EventLog.txt")) Serial.println(F("FTP OK"));
    else Serial.println(F("FTP FAIL"));
  }


//************ MAINLOOP - GPS **********************************************************
  // read data from the GPS in the 'main loop'
  char c = GPS.read();
  // if you want to debug, this is a good time to do it!
  if (GPSECHO)
    if (c) Serial.print(c);

  // Update received GPS dataObject...
  if (GPS.newNMEAreceived()) {
    // a tricky thing here is if we print the NMEA sentence, or data
    // we end up not listening and catching other sentences!
    // so be very wary if using OUTPUT_ALLDATA and trytng to print out data
    // Serial.println(GPS.lastNMEA()); // this also sets the newNMEAreceived() flag to false
    if (!GPS.parse(GPS.lastNMEA())) // this also sets the newNMEAreceived() flag to false
      return; // we can fail to parse a sentence in which case we should just
              // wait for another
  }
  
  if(keypress == 't') {
    OutputDateTime(GPS.hour, GPS.minute, GPS.seconds, GPS.day, GPS.month, GPS.year, 1);
  }

  if(keypress == 'g') {
    OutputLocation(GPS.longitude, GPS.latitude, GPS.lon, GPS.lat, GPS.fix);
    }
  
}

// END FILE
