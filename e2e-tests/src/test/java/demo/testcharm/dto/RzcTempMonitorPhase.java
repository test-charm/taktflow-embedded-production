package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the RZC temp-monitor harness script. All fields are boxed
 * so unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class RzcTempMonitorPhase {
    private Integer cycles;          // MainFunction calls (default 1)
    private Boolean skipInit;        // skip Swc_TempMonitor_Init (uninitialized guard)
    private Integer tempDc;          // NTC1 temperature (deci-degrees C) injected to IoHwAb
    private Integer temp2Dc;         // NTC2 temperature (deci-degrees C); null = agree with NTC1
    private Boolean ioFault;         // IoHwAb_ReadMotorTemp returns E_NOT_OK
    private Boolean temp2Fail;       // IoHwAb_ReadMotorTemp2 returns E_NOT_OK
}
