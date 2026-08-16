package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the CVC NVM harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class CvcNvmPhase {
    private String op;             // init|storeDtc|loadDtc|readCal|writeCal|corruptDtcCrc|corruptCalCrc|calcCrc
    private Boolean skipInit;      // skip Swc_Nvm_Init (uninitialized guard)
    private Integer repeats;       // storeDtc: store count
    private Integer dtcId;         // storeDtc: DTC event ID
    private Integer status;        // storeDtc: DTC status mask
    private Integer ffMode;        // storeDtc: 0=NULL freeze frame, 1=0xA0+i pattern
    private Integer slot;          // loadDtc/corruptDtcCrc: slot index
    private Boolean nullEntry;     // loadDtc: pass NULL_PTR as entry
    private Boolean nullCal;       // readCal/writeCal: pass NULL_PTR
    private Integer pThreshold;    // writeCal: plausThreshold
    private Integer pDebounce;     // writeCal: plausDebounce
    private Integer stuckThreshold;// writeCal: stuckThreshold
    private Integer stuckCycles;   // writeCal: stuckCycles
    private Integer lut0;          // writeCal: torqueLut[0]
    private Integer dataLen;       // calcCrc: buffer length
    private Boolean nullCrc;       // calcCrc: pass NULL_PTR as data
}
