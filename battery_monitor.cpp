#include <BQ24195.h>
#include "logging.h"

float rawADC;
float voltADC;
float voltBat;

int R1 =  330000;
int R2 = 1000000;

float batteryFullVoltage = 4.2;
float batteryEmptyVoltage = 3.3;

float batteryCapacity = 8;

void batterySetup() {
  analogReference(AR_DEFAULT); 
  analogReadResolution(12);

  PMIC.begin();

  PMIC.setMinimumSystemVoltage(batteryEmptyVoltage);
  PMIC.setChargeVoltage(batteryFullVoltage);

  PMIC.setChargeCurrent(batteryCapacity/2);

  PMIC.enableCharge();
}

// Returns -1 if charging.
int readBatteryPercent() {
  rawADC = analogRead(ADC_BATTERY);
  voltADC = rawADC * (3.3/4095.0);
  voltBat = voltADC * (R1 + R2) / R2;
  int new_batt = (voltBat - batteryEmptyVoltage) * (100) / (batteryFullVoltage - batteryEmptyVoltage);

  float curr_charg = PMIC.getChargeCurrent();
  int chargeStatus = PMIC.chargeStatus();
  
  #ifdef DEBUG
  Serial.print("The ADC on PB09 reads a value of ");
  Serial.print(rawADC);
  Serial.print(" which is equivialent to ");
  Serial.print(voltADC);
  Serial.print("V. This means the battery voltage is ");
  Serial.print(voltBat);
  Serial.print("V. Which is equivalent to a charge level of ");
  Serial.print(new_batt);
  Serial.println("%.");

  Serial.print("ChargeCurrent ");
  Serial.println(curr_charg);
  Serial.print("ChargeStatus ");
  Serial.println(chargeStatus);
  #endif

  return chargeStatus == 0 ? new_batt : -1;
}