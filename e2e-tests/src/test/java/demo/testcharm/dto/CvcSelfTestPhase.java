package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the CVC self-test harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults (every check passes).
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class CvcSelfTestPhase {
    private Boolean spi;        // SelfTest_Hw_SpiLoopback result (True=E_OK)
    private Boolean can;        // SelfTest_Hw_CanLoopback result
    private Boolean nvm;        // SelfTest_Hw_NvmCheck result
    private Boolean oled;       // SelfTest_Hw_OledAck result (non-critical)
    private Boolean mpu;        // SelfTest_Hw_MpuVerify result
    private Boolean canary;     // SelfTest_Hw_CanaryCheck result
    private Boolean ram;        // SelfTest_Hw_RamPattern result
}
