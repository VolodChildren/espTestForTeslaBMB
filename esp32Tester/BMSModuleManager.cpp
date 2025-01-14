#include "config.h"
#include "BMSModuleManager.h"
#include "BMSUtil.h"


extern EEPROMSettings settings;

BMSModuleManager::BMSModuleManager()
{
    for (int i = 1; i <= MAX_MODULE_ADDR; i++) {
        modules[i].setExists(false);
        modules[i].setAddress(i);
    }
    lowestPackVolt = 1000.0f;
    highestPackVolt = 0.0f;
    lowestPackTemp = 200.0f;
    highestPackTemp = -100.0f;
    isFaulted = false;
}

//void BMSModuleManager::balanceCells()
//{  
//    for (int address = 1; address <= MAX_MODULE_ADDR; address++)
//    {
//        if (modules[address].isExisting()) modules[address].balanceCells();
//    }
//}

void BMSModuleManager::balanceCells(uint8_t *balanseOn)
{  
    for (int address = 1; address <= MAX_MODULE_ADDR; address++)
    {
        if (modules[address].isExisting() && 1 == balanseOn[address-1]) modules[address].balanceCells();
    }
}

uint8_t BMSModuleManager::getModulBalanceState(uint8_t address, uint8_t number){
    if(modules[address].isExisting()){
      return modules[address].getBalanceState(number);
    }
 }

void BMSModuleManager::balanceCellsOff()
{  
    for (int address = 1; address <= MAX_MODULE_ADDR; address++)
    {
        if (modules[address].isExisting()) modules[address].balanceCellsOff();
    }
}

// void BMSModuleManager::balanceCells()
// {  
//     for (int address = 1; address <= MAX_MODULE_ADDR; address++)
//     {
//         if (modules[address].isExisting()) modules[address].balanceCells();
//     }
// }

/*
 * Try to set up any unitialized boards. Send a command to address 0 and see if there is a response. If there is then there is
 * still at least one unitialized board. Go ahead and give it the first ID not registered as already taken.
 * If we send a command to address 0 and no one responds then every board is inialized and this routine stops.
 * Don't run this routine until after the boards have already been enumerated.\
 * Note: The 0x80 conversion it is looking might in theory block the message from being forwarded so it might be required
 * To do all of this differently. Try with multiple boards. The alternative method would be to try to set the next unused
 * address and see if any boards respond back saying that they set the address. 
 */
void BMSModuleManager::setupBoards()
{
    uint8_t payload[3];
    uint8_t buff[10];
    int retLen;

    payload[0] = 0;
    payload[1] = 0;
    payload[2] = 1;

    while (1 == 1)
    {
        payload[0] = 0;
        payload[1] = 0;
        payload[2] = 1;
        retLen = BMSUtil::sendDataWithReply(payload, 3, false, buff, 4);
        if (retLen == 4)
        {
            if (buff[0] == 0x80 && buff[1] == 0 && buff[2] == 1)
            {
                Serial.println("00 found");
                //look for a free address to use
                for (int y = 1; y < 63; y++) 
                {
                    if (!modules[y].isExisting())
                    {
                        payload[0] = 0;
                        payload[1] = REG_ADDR_CTRL;
                        payload[2] = y | 0x80;
                        BMSUtil::sendData(payload, 3, true);
                        delay(3);
                        if (BMSUtil::getReply(buff, 10) > 2)
                        {
                            if (buff[0] == (0x81) && buff[1] == REG_ADDR_CTRL && buff[2] == (y + 0x80)) 
                            {
                                modules[y].setExists(true);
                                numFoundModules++;
                                Serial.println("Address assigned = ");
                                Serial.println(y);
                            }
                        }
                        break; //quit the for loop
                    }
                }
            }
            else break; //nobody responded properly to the zero address so our work here is done.
        }
        else break;
    }
}

/*
 * Iterate through all 62 possible board addresses (1-62) to see if they respond
 */
void BMSModuleManager::findBoards()
{
    uint8_t payload[3];
    uint8_t buff[8];

    numFoundModules = 0;
    payload[0] = 0;
    payload[1] = 0; //read registers starting at 0
    payload[2] = 1; //read one byte
    for (int x = 1; x <= MAX_MODULE_ADDR; x++)
    {
        modules[x].setExists(false);
        payload[0] = x << 1;
        BMSUtil::sendData(payload, 3, false);
        delay(20);
        if (BMSUtil::getReply(buff, 8) > 4)
        {
            if (buff[0] == (x << 1) && buff[1] == 0 && buff[2] == 1 && buff[4] > 0) {
                modules[x].setExists(true);
                numFoundModules++;
                // Serial.println("Found module with address: %X", x); 
            }
        }
        delay(5);
    }
}


/*
 * Force all modules to reset back to address 0 then set them all up in order so that the first module
 * in line from the master board is 1, the second one 2, and so on.
*/
int BMSModuleManager::renumberBoardIDs()
{

    uint8_t count = 0;
    uint8_t payload[3];
    uint8_t buff[8];
    int attempts = 1;

    for (int y = 1; y < 63; y++) 
    {
        modules[y].setExists(false);
        numFoundModules = 0;
    }

    while (attempts < 3)
    {
        payload[0] = 0x3F << 1; //broadcast the reset command
        payload[1] = 0x3C;//reset
        payload[2] = 0xA5;//data to cause a reset
        BMSUtil::sendData(payload, 3, true);
        delay(100);
        BMSUtil::getReply(buff, 8);
        if (buff[0] == 0x7F && buff[1] == 0x3C && buff[2] == 0xA5 && buff[3] == 0x57) break;
        else count++;
        attempts++;
    }
    if(count == 2){
      return 0;
    }
    setupBoards();
    return 1;
}

/*
After a RESET boards have their faults written due to the hard restart or first time power up, this clears thier faults
*/
void BMSModuleManager::clearFaults()
{
    uint8_t payload[3];
    uint8_t buff[8];
    payload[0] = 0x7F; //broadcast
    payload[1] = REG_ALERT_STATUS;//Alert Status
    payload[2] = 0xFF;//data to cause a reset
    BMSUtil::sendDataWithReply(payload, 3, true, buff, 4);

    payload[0] = 0x7F; //broadcast
    payload[2] = 0x00;//data to clear
    BMSUtil::sendDataWithReply(payload, 3, true, buff, 4);

    payload[0] = 0x7F; //broadcast
    payload[1] = REG_FAULT_STATUS;//Fault Status
    payload[2] = 0xFF;//data to cause a reset
    BMSUtil::sendDataWithReply(payload, 3, true, buff, 4);

    payload[0] = 0x7F; //broadcast
    payload[2] = 0x00;//data to clear
    BMSUtil::sendDataWithReply(payload, 3, true, buff, 4);

    isFaulted = false;
}

/*
Puts all boards on the bus into a Sleep state, very good to use when the vehicle is a rest state. 
Pulling the boards out of sleep only to check voltage decay and temperature when the contactors are open.
*/

void BMSModuleManager::sleepBoards()
{
    uint8_t payload[3];
    uint8_t buff[8];
    payload[0] = 0x7F; //broadcast
    payload[1] = REG_IO_CTRL;//IO ctrl start
    payload[2] = 0x04;//write sleep bit
    BMSUtil::sendData(payload, 3, true);
    delay(2);
    BMSUtil::getReply(buff, 8);
}

/*
Wakes all the boards up and clears thier SLEEP state bit in the Alert Status Registery
*/

void BMSModuleManager::wakeBoards()
{
    uint8_t payload[3];
    uint8_t buff[8];
    payload[0] = 0x7F; //broadcast
    payload[1] = REG_IO_CTRL;//IO ctrl start
    payload[2] = 0x00;//write sleep bit
    BMSUtil::sendData(payload, 3, true);
    delay(2);
    BMSUtil::getReply(buff, 8);
  
    payload[0] = 0x7F; //broadcast
    payload[1] = REG_ALERT_STATUS;//Fault Status
    payload[2] = 0x04;//data to cause a reset
    BMSUtil::sendData(payload, 3, true);
    delay(2);
    BMSUtil::getReply(buff, 8);
    payload[0] = 0x7F; //broadcast
    payload[2] = 0x00;//data to clear
    BMSUtil::sendData(payload, 3, true);
    delay(2);
    BMSUtil::getReply(buff, 8);
}
float BMSModuleManager::getVoltCell(int index,float* volt){
     volt[0] = modules[index].getCellVoltage(0);
     volt[1] = modules[index].getCellVoltage(1);
     volt[2] = modules[index].getCellVoltage(2);
     volt[3] = modules[index].getCellVoltage(3);
     volt[4] = modules[index].getCellVoltage(4);
     volt[5] = modules[index].getCellVoltage(5);
}
float BMSModuleManager::getTempCell(int index,float* temp){
     temp[0] = modules[index].getTemperature(0);
     temp[1] = modules[index].getTemperature(1);
}

bool BMSModuleManager::getStatusModule(int index){
    return modules[index].isExisting();
}
bool BMSModuleManager::getAllVoltTemp()
{
  bool ret =false; 
    packVolt = 0.0f;
    for (int x = 1; x <= MAX_MODULE_ADDR; x++)
    {
        if (modules[x].isExisting()) 
        {
//             Serial.println("hgjkgjh");
//             Serial.print("Module "); //%i ", x);
//             Serial.print(x);
//             Serial.println(" exists. Reading voltage and temperature values");
            ret = modules[x].readModuleValues();
//             Serial.print("Module voltage:");
//             Serial.println(modules[x].getModuleVoltage());
//             Serial.print("Lowest Cell V:"); //%f     , , );
//             Serial.print(modules[x].getLowCellV());
//             Serial.print("  wwHighest Cell V:");
//             Serial.println(modules[x].getHighCellV());\
//             Serial.print("Temp1:"); //%f     , , );
//             Serial.print(modules[x].getTemperature(0));
//             Serial.print("  Temp2:");
//             Serial.println( modules[x].getTemperature(1));
////             //Serial.println(" %f       Temp2: %f", modules[x].getTemperature(0), modules[x].getTemperature(1));
//             Serial.print(modules[x].getCellVoltage(0),3);
//             Serial.print(" ");
//             Serial.print(modules[x].getCellVoltage(1),3);
//             Serial.print(" ");
//            
//             Serial.print(modules[x].getCellVoltage(2),3);
//             Serial.print(" ");
//           
//             Serial.print(modules[x].getCellVoltage(3),3);
//             Serial.print(" ");
//          
//             Serial.print(modules[x].getCellVoltage(4),3);
//             Serial.print(" ");
//          
//             Serial.print(modules[x].getCellVoltage(5),3);


            packVolt += modules[x].getModuleVoltage();
            if (modules[x].getLowTemp() < lowestPackTemp) lowestPackTemp = modules[x].getLowTemp();
            if (modules[x].getHighTemp() > highestPackTemp) highestPackTemp = modules[x].getHighTemp();            
        }
    }

    if (packVolt > highestPackVolt) highestPackVolt = packVolt;
    if (packVolt < lowestPackVolt) lowestPackVolt = packVolt;

    if (digitalRead(22) == LOW) {
        // Serial.print("Fault LOW");
        isFaulted = true;
    }
    else
    {
        // Serial.print("Fault HIGHT");
        isFaulted = false;
    }

    return ret;
}

float BMSModuleManager::getPackVoltage()
{
    return packVolt;
}

float BMSModuleManager::getAvgTemperature()
{
    float avg = 0.0f;    
    for (int x = 1; x <= MAX_MODULE_ADDR; x++)
    {
        if (modules[x].isExisting()) avg += modules[x].getAvgTemp();
    }
    avg = avg / (float)numFoundModules;

    return avg;
}

float BMSModuleManager::getAvgCellVolt()
{
    float avg = 0.0f;    
    for (int x = 1; x <= MAX_MODULE_ADDR; x++)
    {
        if (modules[x].isExisting()) avg += modules[x].getAverageV();
    }
    avg = avg / (float)numFoundModules;

    return avg;
}

void BMSModuleManager::printPackSummary()
{
    uint8_t faults;
    uint8_t alerts;
    uint8_t COV;
    uint8_t CUV;

    //Logger::.+;
    //Logger::.+;
    //Logger::.+;
    //Logger::.+;
   // if (isFaulted) //Logger::.+;
   // //else //Logger::.+;
    // Logger::console("Modules: %i    Voltage: %fV   Avg Cell Voltage: %fV     Avg Temp: %fC ", numFoundModules, 
                    // getPackVoltage(),getAvgCellVolt(), getAvgTemperature());
    //Logger::.+;
    for (int y = 1; y < 63; y++)
    {
        if (modules[y].isExisting())
        {
            faults = modules[y].getFaults();
            alerts = modules[y].getAlerts();
            COV = modules[y].getCOVCells();
            CUV = modules[y].getCUVCells();

            //Logger::.+;

            // Logger::console("  Voltage: %fV   (%fV-%fV)     Temperatures: (%fC-%fC)", modules[y].getModuleVoltage(), 
            //                 modules[y].getLowCellV(), modules[y].getHighCellV(), modules[y].getLowTemp(), modules[y].getHighTemp());

            //SerialUSB.print("  Currently balancing cells: ");
            for (int i = 0; i < 6; i++)
            {                
                if (modules[y].getBalancingState(i) == 1) 
                {                    
                    //SerialUSB.print(i);
                    //SerialUSB.print(" ");
                }
            }
            //SerialUSB.println();

            if (faults > 0)
            {
                //Logger::.+;
                if (faults & 1)
                {
                    //SerialUSB.print("    Overvoltage Cell Numbers (1-6): ");
                    for (int i = 0; i < 6; i++)
                    {
                        if (COV & (1 << i)) 
                        {
                            //SerialUSB.print(i+1);
                            //SerialUSB.print(" ");
                        }
                    }
                    //SerialUSB.println();
                }
                if (faults & 2)
                {
                    //SerialUSB.print("    Undervoltage Cell Numbers (1-6): ");
                    for (int i = 0; i < 6; i++)
                    {
                        if (CUV & (1 << i)) 
                        {
                            //SerialUSB.print(i+1);
                            //SerialUSB.print(" ");
                        }
                    }
                    //SerialUSB.println();
                }
                if (faults & 4)
                {
                    //Logger::.+;
                }
                if (faults & 8)
                {
                    //Logger::.+;
                }
                if (faults & 0x10)
                {
                    //Logger::.+;
                }
                if (faults & 0x20)
                {
                    //Logger::.+;
                }
            }
            if (alerts > 0)
            {
                //Logger::.+;
                if (alerts & 1)
                {
                    //Logger::.+;
                }
                if (alerts & 2)
                {
                    //Logger::.+;
                }
                if (alerts & 4)
                {
                    //Logger::.+;
                }
                if (alerts & 8)
                {
                    //Logger::.+;
                }
                if (alerts & 0x10)
                {
                    //Logger::.+;
                }
                if (alerts & 0x20)
                {
                    //Logger::.+;
                }
                if (alerts & 0x40)
                {
                    //Logger::.+;
                }
                if (alerts & 0x80)
                {
                    //Logger::.+;
                }
            }
            if (faults > 0 || alerts > 0); //SerialUSB.println();
        }
    }
}

void BMSModuleManager::printPackDetails()
{
    uint8_t faults;
    uint8_t alerts;
    uint8_t COV;
    uint8_t CUV;
    int cellNum = 0;

    //Logger::.+;
    //Logger::.+;
    //Logger::.+;
    //Logger::.+;
    // if (isFaulted) //Logger::.+;
    // //else //Logger::.+;
    // Logger::console("Modules: %i    Voltage: %fV   Avg Cell Voltage: %fV     Avg Temp: %fC ", numFoundModules, 
    //                 getPackVoltage(),getAvgCellVolt(), getAvgTemperature());
    //Logger::.+;
    for (int y = 1; y < 63; y++)
    {
        if (modules[y].isExisting())
        {
            faults = modules[y].getFaults();
            alerts = modules[y].getAlerts();
            COV = modules[y].getCOVCells();
            CUV = modules[y].getCUVCells();

            //SerialUSB.print("Module #");
            //SerialUSB.print(y);
            if (y < 10) //SerialUSB.print(" ");
            //SerialUSB.print("  ");
            //SerialUSB.print(modules[y].getModuleVoltage());
            //SerialUSB.print("V");
            for (int i = 0; i < 6; i++)
            {
                if (cellNum < 10) //SerialUSB.print(" ");
                //SerialUSB.print("  Cell");
                //SerialUSB.print(cellNum++);
                //SerialUSB.print(": ");
                //SerialUSB.print(modules[y].getCellVoltage(i));
                //SerialUSB.print("V");
                if (modules[y].getBalancingState(i) == 1); //SerialUSB.print("*");
                else; //SerialUSB.print(" ");
            }
            //SerialUSB.print("  Neg Term Temp: ");
            //SerialUSB.print(modules[y].getTemperature(0));
            //SerialUSB.print("C  Pos Term Temp: ");
            //SerialUSB.print(modules[y].getTemperature(1)); 
            //SerialUSB.println("C");
        }
    }
}

void BMSModuleManager::lcdPrintf(){
    // uint8_t r = 255, g = 255, b = 255;
    // uint16_t color;
    // float data[4][6];
    // float tempData[4][2];

    // uint16_t ID = tft.readID();
    // tft.begin(ID);
    // tft.invertDisplay(false);
    // tft.setRotation(1);


    // tft.fillScreen(BLACK);

    // color = tft.color565(255, 255, 255);
    // tft.setTextColor(color);

    // for(int i =0 ; i<2; i++){
    //     char ss[3] ;
    //     char dataS[3] ;
    //     sprintf(ss, "M1%d", i);
    //     getCellVoltage
    //     sprintf(dataS, "%.3fv %.3fv %.3fv T1 %.0f°c \n%.3fv %.3fv %.3fv T2 %.0f°c", module[i].getCellVoltage(0),    
    //                                                                                 module[i].getCellVoltage(1),
    //                                                                                 module[i].getCellVoltage(2), 
    //                                                                                 12.0,
    //                                                                                 module[i].getCellVoltage(3), 
    //                                                                                 module[i].getCellVoltage(4), 
    //                                                                                 module[i].getCellVoltage(5),  
    //                                                                                 12.0);
    //     showmsgXY(0, 20+50*i, 1, &FreeSans9pt7b, dataS); 
    //     showmsgXY(245, 35+50*i, 2, &FreeSans9pt7b, ss); 
    //     showmsgXY(0, 52+50*i, 1, &FreeSans9pt7b, "-----------------------------------------------------"); 
    // }
    // showmsgXY(0, 212, 1, &FreeSans9pt7b, "-----------------------------------------------------"); 
    // showmsgXY(0, 236, 2, &FreeSans9pt7b, "\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/"); 
}
// void BMSModuleManager::processCANMsg(CAN_FRAME &frame)
// {
//     uint8_t battId = (frame.id >> 16) & 0xF;
//     uint8_t moduleId = (frame.id >> 8) & 0xFF;
//     uint8_t cellId = (frame.id) & 0xFF;
    
//     if (moduleId = 0xFF)  //every module
//     {
//         if (cellId == 0xFF) sendBatterySummary();        
//         else 
//         {
//             for (int i = 1; i <= MAX_MODULE_ADDR; i++) 
//             {
//                 if (modules[i].isExisting()) 
//                 {
//                     sendCellDetails(i, cellId);
//                     delayMicroseconds(500);
//                 }
//             }
//         }
//     }
//     else //a specific module
//     {
//         if (cellId == 0xFF) sendModuleSummary(moduleId);
//         else sendCellDetails(moduleId, cellId);
//     }
// }

// void BMSModuleManager::sendBatterySummary()
// {
//     CAN_FRAME outgoing;
//     outgoing.id = (0x1BA00000ul) + ((settings.batteryID & 0xF) << 16) + 0xFFFF;
//     outgoing.rtr = 0;
//     outgoing.priority = 1;
//     outgoing.extended = true;
//     outgoing.length = 8;

//     uint16_t battV = uint16_t(getPackVoltage() * 100.0f);
//     outgoing.data.byte[0] = battV & 0xFF;
//     outgoing.data.byte[1] = battV >> 8;
//     outgoing.data.byte[2] = 0;  //instantaneous current. Not measured at this point
//     outgoing.data.byte[3] = 0;
//     outgoing.data.byte[4] = 50; //state of charge
//     int avgTemp = (int)getAvgTemperature() + 40;
//     if (avgTemp < 0) avgTemp = 0;
//     outgoing.data.byte[5] = avgTemp;
//     avgTemp = (int)lowestPackTemp + 40;
//     if (avgTemp < 0) avgTemp = 0;    
//     outgoing.data.byte[6] = avgTemp;
//     avgTemp = (int)highestPackTemp + 40;
//     if (avgTemp < 0) avgTemp = 0;
//     outgoing.data.byte[7] = avgTemp;
//     Can0.sendFrame(outgoing);
// }

// void BMSModuleManager::sendModuleSummary(int module)
// {
//     CAN_FRAME outgoing;
//     outgoing.id = (0x1BA00000ul) + ((settings.batteryID & 0xF) << 16) + ((module & 0xFF) << 8) + 0xFF;
//     outgoing.rtr = 0;
//     outgoing.priority = 1;
//     outgoing.extended = true;
//     outgoing.length = 8;

//     uint16_t battV = uint16_t(modules[module].getModuleVoltage() * 100.0f);
//     outgoing.data.byte[0] = battV & 0xFF;
//     outgoing.data.byte[1] = battV >> 8;
//     outgoing.data.byte[2] = 0;  //instantaneous current. Not measured at this point
//     outgoing.data.byte[3] = 0;
//     outgoing.data.byte[4] = 50; //state of charge
//     int avgTemp = (int)modules[module].getAvgTemp() + 40;
//     if (avgTemp < 0) avgTemp = 0;
//     outgoing.data.byte[5] = avgTemp;
//     avgTemp = (int)modules[module].getLowestTemp() + 40;
//     if (avgTemp < 0) avgTemp = 0;
//     outgoing.data.byte[6] = avgTemp;
//     avgTemp = (int)modules[module].getHighestTemp() + 40;
//     if (avgTemp < 0) avgTemp = 0;
//     outgoing.data.byte[7] = avgTemp;

//     Can0.sendFrame(outgoing);
// }

// void BMSModuleManager::sendCellDetails(int module, int cell)
// {
//     CAN_FRAME outgoing;
//     outgoing.id = (0x1BA00000ul) + ((settings.batteryID & 0xF) << 16) + ((module & 0xFF) << 8) + (cell & 0xFF);
//     outgoing.rtr = 0;
//     outgoing.priority = 1;
//     outgoing.extended = true;
//     outgoing.length = 8;

//     uint16_t battV = uint16_t(modules[module].getCellVoltage(cell) * 100.0f);
//     outgoing.data.byte[0] = battV & 0xFF;
//     outgoing.data.byte[1] = battV >> 8;
//     battV = uint16_t(modules[module].getHighestCellVolt(cell) * 100.0f);
//     outgoing.data.byte[2] = battV & 0xFF;
//     outgoing.data.byte[3] = battV >> 8;
//     battV = uint16_t(modules[module].getLowestCellVolt(cell) * 100.0f);
//     outgoing.data.byte[4] = battV & 0xFF;
//     outgoing.data.byte[5] = battV >> 8;
//     int instTemp = modules[module].getHighTemp() + 40;
//     outgoing.data.byte[6] = instTemp; // should be nearest temperature reading not highest but this works too.
//     outgoing.data.byte[7] = 0; //Bit encoded fault data. No definitions for this yet.

//     Can0.sendFrame(outgoing);
// }

// //The SerialConsole actually sets the battery ID to a specific value. We just have to set up the CAN filter here to
// //match.
// void BMSModuleManager::setBatteryID()
// {
//     //Setup filter for direct access to our registered battery ID
//     uint32_t canID = (0xBAul << 20) + (((uint32_t)settings.batteryID & 0xF) << 16);
//     Can0.setRXFilter(0, canID, 0x1FFF0000ul, true);
// }
