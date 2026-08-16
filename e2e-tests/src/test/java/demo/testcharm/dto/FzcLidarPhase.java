package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the FZC lidar harness script. All fields are boxed
 * so unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class FzcLidarPhase {
    private Integer cycles;          // MainFunction calls (default 1)
    private Boolean skipInit;        // skip Swc_Lidar_Init (uninitialized guard)
    private Boolean initNull;        // Swc_Lidar_Init(NULL) (NULL-config guard)
    private Integer distCm;          // distance in cm for the injected TFMini-S frame
    private Integer signal;          // signal strength for the injected frame
    private Boolean noFrame;         // feed no UART bytes (timeout path)
    private Boolean badChecksum;     // corrupt the frame checksum byte
    private Boolean garbageHeader;   // feed 32 non-header bytes (sync fail)
    private Boolean partialFrame;    // feed header + 3 bytes only (incomplete)
    private Integer uartFailAt;      // fail UART reads at/after call index (0=never; 1=sync, 3=payload)
    private Boolean getDist;         // call Swc_Lidar_GetDistance at end of phase
    private Boolean getDistNull;     // call Swc_Lidar_GetDistance(NULL)
}
