package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the RZC self-test harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults (every check passes, no skip flags).
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class RzcSelfTestPhase {
    private Boolean skipInit;   // skip Swc_RzcSelfTest_Init (uninitialized guard)
    private Boolean initNull;   // call Swc_RzcSelfTest_Init(NULL_PTR) (NULL-config guard)
    private Integer bts7960;    // BTS7960 enable-pin toggle (1=E_OK 0=E_NOT_OK 2=NULL)
    private Integer acs723;     // ACS723 baseline calibration
    private Integer ntc;        // NTC temperature range check
    private Integer encoder;    // Encoder connectivity
    private Integer can;        // CAN loopback
    private Integer mpu;        // MPU region verify
    private Integer canary;     // Stack canary plant
    private Integer ram;        // RAM pattern test
}
