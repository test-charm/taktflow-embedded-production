package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the RZC NVM harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class RzcNvmPhase {
    private String op;                  // init|storeDtc|loadDtc|corruptDtcCrc|calcCrc
    private Boolean skipInit;           // skip Swc_RzcNvm_Init on harness start
    private Integer repeats;            // storeDtc: repeat count
    private Integer dtcId;              // storeDtc: DTC event ID
    private Integer status;             // storeDtc: DTC status byte
    private Integer timestamp;          // storeDtc: system tick at storage
    private Integer motorCurrentMa;     // storeDtc: freeze-frame motor current (mA)
    private Integer motorTempDdc;       // storeDtc: freeze-frame motor temp (deci-deg C)
    private Integer motorSpeedRpm;      // storeDtc: freeze-frame motor speed (RPM)
    private Integer batteryMv;          // storeDtc: freeze-frame battery voltage (mV)
    private Integer torqueCmdPct;       // storeDtc: freeze-frame torque command (%)
    private Integer vehicleState;       // storeDtc: freeze-frame vehicle state
    private Integer slot;               // loadDtc/corruptDtcCrc: slot index
    private Boolean nullFreeze;         // storeDtc: pass NULL_PTR
    private Boolean nullEntry;          // loadDtc: pass NULL_PTR
    private Integer dataLen;            // calcCrc: buffer length
}
