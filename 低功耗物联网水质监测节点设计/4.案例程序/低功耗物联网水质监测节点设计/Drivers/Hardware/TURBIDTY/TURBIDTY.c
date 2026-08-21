#include ".\Hardware\TURBIDTY\TURBIDTY.h"
#include ".\Hardware\BoardADC\BoardADC.h"
#include <math.h>

#define TURBIDITY_MAX_NTU 3000.0f
#define TURBIDITY_ADC_TO_SENSOR_GAIN (37.0f / 27.0f)
#define TURBIDITY_SENSOR_MIN_VOLTAGE 0.5f
#define TURBIDITY_SENSOR_MAX_VOLTAGE 4.5f

static float Turbidity_ClampFloat(float value, float min_value, float max_value)
{
    if (value < min_value)
    {
        return min_value;
    }

    if (value > max_value)
    {
        return max_value;
    }

    return value;
}

static float Turbidity_ReadSensorVoltage(void)
{
    float voltage = BoardADC_ReadVoltage(BOARD_ADC_TURBIDITY) * TURBIDITY_ADC_TO_SENSOR_GAIN;

    return Turbidity_ClampFloat(voltage, TURBIDITY_SENSOR_MIN_VOLTAGE, TURBIDITY_SENSOR_MAX_VOLTAGE);
}

/*
 * Remap the real sensor window (0.5V..4.5V) into the monotonic section of
 * the fitted calibration curve, then evaluate the host-provided coefficients.
 * This keeps the existing a/b/c interface while avoiding the hard 0 / max
 * collapse caused by clamping directly on the raw quadratic roots.
 */
static float Turbidity_ConvertVoltageToNTU(float voltage, float a, float b, float c)
{
    float calibration_voltage = Turbidity_ClampFloat(voltage, TURBIDITY_SENSOR_MIN_VOLTAGE, TURBIDITY_SENSOR_MAX_VOLTAGE);
    float ntu;

    if (a < 0.0f)
    {
        float discriminant = (b * b) - (4.0f * a * c);

        if (discriminant > 0.0f)
        {
            float sqrt_discriminant = sqrtf(discriminant);
            float denom = 2.0f * a;
            float root_low = (-b - sqrt_discriminant) / denom;
            float root_high = (-b + sqrt_discriminant) / denom;
            float vertex = -b / denom;
            float actual_span = TURBIDITY_SENSOR_MAX_VOLTAGE - TURBIDITY_SENSOR_MIN_VOLTAGE;
            float cal_span;
            float normalized;
            float tmp;

            if (root_low > root_high)
            {
                tmp = root_low;
                root_low = root_high;
                root_high = tmp;
            }

            if ((vertex > root_low) && (root_high > vertex) && (actual_span > 0.0f))
            {
                cal_span = vertex - root_low;
                normalized = (TURBIDITY_SENSOR_MAX_VOLTAGE - calibration_voltage) / actual_span;
                normalized = Turbidity_ClampFloat(normalized, 0.0f, 1.0f);

                calibration_voltage = root_low + (normalized * cal_span);
            }
        }
    }

    ntu = (a * calibration_voltage * calibration_voltage) + (b * calibration_voltage) + c;
    return Turbidity_ClampFloat(ntu, 0.0f, TURBIDITY_MAX_NTU);
}

void Turbidity_Init(void)
{
    (void)BoardADC_Init();
}

float Turbidity_ReadVoltage(void)
{
    return Turbidity_ReadSensorVoltage();
}

float Turbidity_ReadNTU(float a, float b, float c)
{
    float voltage = Turbidity_ReadVoltage();
    return Turbidity_ConvertVoltageToNTU(voltage, a, b, c);
}

Turbidity_Data_t Turbidity_Read(float a, float b, float c)
{
    Turbidity_Data_t data;

    data.voltage_v = Turbidity_ReadVoltage();
    data.ntu = Turbidity_ConvertVoltageToNTU(data.voltage_v, a, b, c);

    return data;
}
