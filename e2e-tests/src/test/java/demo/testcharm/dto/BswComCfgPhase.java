package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the BSW Com config true end-to-end endpoint. All fields are
 * boxed so unspecified fields become {@code null} and are omitted from the
 * request JSON (NON_NULL); the server side then applies its defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class BswComCfgPhase {
    private String op;          // bus-probe
    private java.util.List<String> targets;   // DBC message names to observe
    private Integer windowMs;                 // observation window
    private Integer minFrames;                // minimum frames per message
    private Integer periodTolerancePct;       // allowed cycle-time deviation
}
