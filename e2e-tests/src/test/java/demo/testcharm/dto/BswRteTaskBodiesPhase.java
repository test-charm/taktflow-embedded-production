package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the BSW RTE task-bodies harness script. All fields are boxed
 * so unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class BswRteTaskBodiesPhase {
    private String op;          // run
    private String task;        // 1ms|10ms|50ms|100ms|5000ms|idle
    private Integer idleIters;  // idle: bounded loop iterations (default 3)
}
