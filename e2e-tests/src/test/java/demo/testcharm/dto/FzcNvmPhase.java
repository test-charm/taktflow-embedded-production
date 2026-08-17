package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the FZC NVM harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class FzcNvmPhase {
    private String op;                  // init|storeDtc|loadDtc|readCal|writeCal|corruptDtcCrc|corruptCalCrc|corruptBackendCalCrc|calcCrc
    private Boolean skipInit;           // skip Swc_FzcNvm_Init on harness start
    private Integer repeats;            // storeDtc: repeat count
    private Integer dtcId;              // storeDtc: DTC ID
    private Integer steerAngle;         // storeDtc: freezeSteer
    private Integer brakePos;           // storeDtc: freezeBrake
    private Integer lidarDist;          // storeDtc: freezeLidar
    private Integer slot;               // loadDtc/corruptDtcCrc: slot index
    private Boolean nullRecord;         // loadDtc: pass NULL_PTR
    private Boolean nullCal;            // readCal/writeCal: pass NULL_PTR
    private Integer steerCenterOffset;  // writeCal
    private Integer steerGain;          // writeCal
    private Integer brakePosOffset;     // writeCal
    private Integer brakeGain;          // writeCal
    private Integer lidarWarnCm;        // writeCal
    private Integer lidarBrakeCm;       // writeCal
    private Integer lidarEmergencyCm;   // writeCal
    private Integer dataLen;            // calcCrc: buffer length
    private Boolean nullCrc;            // calcCrc: pass NULL_PTR
}
