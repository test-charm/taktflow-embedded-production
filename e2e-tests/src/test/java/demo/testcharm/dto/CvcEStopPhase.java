package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the CVC E-stop harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request JSON
 * (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class CvcEStopPhase {
    private Integer cycles;
    private Integer pin;         // 0=LOW (released), 1=HIGH (pressed)
    private Boolean readFail;    // IoHwAb read failure → fail-safe active
    private Boolean skipInit;    // skip Swc_EStop_Init (uninitialized no-op guard)
}
