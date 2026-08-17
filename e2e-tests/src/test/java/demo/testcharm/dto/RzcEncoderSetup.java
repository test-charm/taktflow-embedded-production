package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * RZC encoder phase script. Each phase drives one segment of the
 * {@code Swc_Encoder_MainFunction} cycle in the native encoder harness.
 */
@Getter
@Setter
public class RzcEncoderSetup {
    private List<RzcEncoderPhase> phases;
}
