package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the SC state harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class ScStatePhase {
    private String op;              // init|transition|setRaw
    private Boolean skipInit;       // skip SC_State_Init on harness start
    private Integer newState;       // transition: target state (SC_STATE_*)
    private Integer state;          // setRaw: raw state value to inject
}
