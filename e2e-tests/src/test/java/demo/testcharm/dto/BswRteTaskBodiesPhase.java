package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the BSW RTE task-bodies true end-to-end endpoint. All fields
 * are boxed so unspecified fields become {@code null} and are omitted from
 * the request JSON (NON_NULL); the server side then applies its defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class BswRteTaskBodiesPhase {
    private String op;          // cadence|ftti
    // cadence (true end-to-end, vcan0 bus observation)
    private java.util.List<String> targets;   // DBC message names to observe
    private Integer windowMs;                 // observation window
    private Integer minFrames;                // minimum frames per message
    private Integer periodTolerancePct;       // allowed cycle-time deviation
    // ftti (true end-to-end E-stop latency)
    private Integer budgetMs;                 // E-stop FTTI budget
    private Boolean restartCvc;               // restart CVC to clear latch
}
