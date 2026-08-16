package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * FZC lidar phase script. Each phase drives one segment of the
 * {@code Swc_Lidar_MainFunction} cycle in the native lidar harness.
 */
@Getter
@Setter
public class FzcLidarSetup {
    private List<FzcLidarPhase> phases;
}
