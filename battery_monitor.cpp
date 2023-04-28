#include <BQ24195.h>

float rawADC;
float voltADC;
float voltBat;

int R1 =  330000;
int R2 = 1000000;

int max_Source_voltage;

float batteryFullVoltage = 4.2;
float batteryEmptyVoltage = 3.3;

float batteryCapacity = 8;

void batterySetup() {
  Serial.begin(9600);       

  analogReference(AR_DEFAULT); 
  analogReadResolution(12);

  PMIC.begin();

  PMIC.setMinimumSystemVoltage(batteryEmptyVoltage);
  PMIC.setChargeVoltage(batteryFullVoltage);

  PMIC.setChargeCurrent(batteryCapacity/2);

  PMIC.enableCharge();

  max_Source_voltage = (3.3 * (R1 + R2))/R2;
}

// Returns -1 if charging.
int readBatteryPercent() {
  // Disable charging and wait a bit to get a proper voltage reading.

  rawADC = analogRead(ADC_BATTERY);
  voltADC = rawADC * (3.3/4095.0);
  voltBat = voltADC * (max_Source_voltage/3.3);
  int new_batt = (voltBat - batteryEmptyVoltage) * (100) / (batteryFullVoltage - batteryEmptyVoltage);

  float curr_charg = PMIC.getChargeCurrent();
  int chargeStatus = PMIC.chargeStatus();
  
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

  return chargeStatus == 0 ? new_batt : -1;
}