package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the SC self-test harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class ScSelfTestPhase {
    private String op;              // init|startup|runtime|canary
    private Integer b1;             // startup: hw_lockstep_bist (0|1)
    private Integer b2;             // startup: hw_ram_pbist (0|1)
    private Integer b3;             // startup: hw_flash_crc_check (0|1)
    private Integer b4;             // startup: hw_dcan_loopback_test (0|1)
    private Integer b5;             // startup: hw_gpio_readback_test (0|1)
    private Integer b6;             // startup: hw_lamp_test (0|1)
    private Integer b7;             // startup: hw_watchdog_test (0|1)
    private Integer flashIncr;      // runtime: hw_flash_crc_incremental (0|1)
    private Integer dcanErr;        // runtime: hw_dcan_error_check (0|1)
    private Integer readback;       // runtime: GIO relay pin readback (0|1)
    private Integer corruptCanary;  // canary: corrupt stack canary before check
    private Integer corruptRam;     // runtime: corrupt RAM test area byte 0
    private Integer repeats;        // runtime: SC_SelfTest_Runtime call count
}
