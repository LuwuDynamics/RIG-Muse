// Extracted unmodified from RIG-Omni puppy/xgo.cc move().
void move(){
    float ratio = 0.0;
    float step = 0.0;
    static float pace_t = 0.0;
    int x_index = 0;
    int yaw_index = 2;
    if(pace_t > 2.0*PI){
        pace_t = 0.0;
    }
    // pace_t += 0.077;
    pace_t += 0.085;
    step = sqrt(vx*vx+vyaw*vyaw);

    if(vx>0){
        x_index = 0;
    }else{
        x_index = 1;
    }
    if(vyaw>0){
        yaw_index = 3;
    }else{
        yaw_index = 2;
    }

    if(control_mode == 0){
        if(abs(vx)>15 || abs(vyaw)>15){
            ratio = abs(vx)/(abs(vx) + abs(vyaw));
            motor[0].DesPos = motor[0].ZeroPos - 700 + (short)(step*cos(pace_t + ratio*l_p[x_index][0] + (1-ratio)*l_p[yaw_index][0]));
            motor[1].DesPos = motor[1].ZeroPos + 700 + (short)(step*cos(pace_t + ratio*l_p[x_index][1] + (1-ratio)*l_p[yaw_index][1]));
            motor[2].DesPos = motor[2].ZeroPos - 700 - (short)(step*cos(pace_t + ratio*l_p[x_index][2] + (1-ratio)*l_p[yaw_index][2]));
            motor[3].DesPos = motor[3].ZeroPos + 700 - (short)(step*cos(pace_t + ratio*l_p[x_index][3] + (1-ratio)*l_p[yaw_index][3]));
            motor[4].DesPos = motor[4].ZeroPos + (short)(200.0*cos(pace_t + ratio*l_p[x_index][4] + (1-ratio)*l_p[yaw_index][4]));
            // motor[4].DesPos = motor[4].ZeroPos + (short)(step*1.5*cos(pace_t + ratio*l_p[x_index][4] + (1-ratio)*l_p[yaw_index][4]));
        }else{
            motor[0].DesPos = motor[0].ZeroPos - 550;
            motor[1].DesPos = motor[1].ZeroPos + 550;
            motor[2].DesPos = motor[2].ZeroPos - 550;
            motor[3].DesPos = motor[3].ZeroPos + 550;
            motor[4].DesPos = motor[4].ZeroPos;
        }
    }else{
        set_motor_angle(angle1, angle2, angle3, angle4, angle5);
    }

}
