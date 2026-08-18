package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the SC relay harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class ScRelayPhase {
    private String op;              // init|energize|deEnergize|checkTriggers|setMock|setReadback
    private Boolean skipInit;       // skip SC_Relay_Init on harness start
    private Integer repeats;        // checkTriggers: SC_Relay_CheckTriggers call count
    private Integer estop;          // setMock: SC_CAN_IsEStopActive
    private Integer hb;             // setMock: SC_Heartbeat_IsAnyConfirmed
    private Integer plaus;          // setMock: SC_Plausibility_IsFaulted
    private Integer creep;          // setMock: SC_Plausibility_IsCreepFaulted
    private Integer e2e;            // setMock: SC_E2E_IsAnyCriticalFailed
    private Integer selftest;       // setMock: SC_SelfTest_IsHealthy (1=healthy)
    private Integer esm;            // setMock: SC_ESM_IsErrorActive
    private Integer busoff;         // setMock: SC_CAN_IsBusOff
    private Integer busSilent;      // setMock: SC_CAN_IsBusSilent
    private Integer value;          // setReadback: GIO relay pin readback value
}
