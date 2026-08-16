package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the RZC battery harness script. All fields are boxed
 * so unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class RzcBatteryPhase {
    private Integer cycles;          // MainFunction calls (default 1)
    private Boolean skipInit;        // skip Swc_Battery_Init (uninitialized guard)
    private Integer voltageMv;       // raw battery voltage (mV) injected to IoHwAb
}
