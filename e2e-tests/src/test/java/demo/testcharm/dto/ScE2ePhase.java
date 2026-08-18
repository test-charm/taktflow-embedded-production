package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the SC E2E harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class ScE2ePhase {
    private String op;              // init|check|drainGrace|crc8|compute
    private Boolean skipInit;       // skip SC_E2E_Init on harness start
    private Integer dataId;         // check: E2E Data ID
    private Integer msgIndex;       // check: mailbox index (0-based)
    private Integer dlc;            // check: data length code
    private Integer alive;          // check: alive counter (0-15)
    private Integer crcCorrupt;     // check: flip CRC byte
    private Integer dataIdCorrupt;  // check: force byte0 lower nibble != dataId
    private Integer payloadCorrupt; // check: flip payload byte data[4]
    private Integer nullData;       // check/crc8/compute: pass NULL_PTR
    private Integer ticks;          // drainGrace: IsAnyCriticalFailed call count
    private Integer len;            // crc8/compute: input byte length
}
