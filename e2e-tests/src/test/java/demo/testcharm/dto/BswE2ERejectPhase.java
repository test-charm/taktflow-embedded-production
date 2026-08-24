package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the BSW E2E-rejection true end-to-end endpoint. All fields
 * are boxed so unspecified fields become {@code null} and are omitted from
 * the request JSON (NON_NULL); the server then applies its defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class BswE2ERejectPhase {
    private String op;              // corrupt|escalate
    // corrupt — feed corrupted E2E frames to the equipped CVC RX
    private String target;          // DBC message name (CVC E2E-protected RX)
    private String mode;            // dataid|crc|replay|seq
    private Integer count;          // number of corrupted frames
    private Integer intervalMs;     // injection spacing (ms)
    private Integer settleMs;       // post-injection settle (ms)
    // escalate — SIL-009 family: RZC rejects sustained corruption → DTC 0xE601
    private Integer observeMs;      // DTC observation window (ms)
    private Boolean restartCvc;     // restore the CVC afterwards
}