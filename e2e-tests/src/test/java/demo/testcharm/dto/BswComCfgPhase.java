package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the BSW Com config readback harness script. All fields are
 * boxed so unspecified fields become {@code null} and are omitted from the
 * request JSON (NON_NULL); the server-side harness then applies its
 * production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class BswComCfgPhase {
    private String op;          // dump|txpdu|rxpdu|sig|invariants
    private Integer id;         // txpdu/rxpdu: PDU id; sig: signal id
    private String table;       // sig: tx|rx
}
