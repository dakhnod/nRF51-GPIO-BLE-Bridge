#include "ble_gpio_asm.h"
#include "ble_helpers.h"
#include "encoding.h"
#include "app_error.h"

#include "sequence.h"

uint16_t ble_gpio_asm_connection_handle;
uint16_t ble_gpio_asm_service_handle;
uint16_t ble_gpio_asm_characteristic_data_handle;

uint8_t ble_gpio_asm_custom_uuid_type;

ret_code_t ble_gpio_asm_characteristic_asm_data_add()
{
    return ble_helper_characteristic_add(
        ble_gpio_asm_service_handle,
        UUID_GPIO_ASM_DATA,
        ble_gpio_asm_custom_uuid_type,
        "gpioASM data",
        true,
        false,
        false,
        false,
        true,
        20,
        &ble_gpio_asm_characteristic_data_handle,
        NULL);
}


uint8_t ble_gpio_asm_handle_data_write(uint8_t *data, uint32_t length)
{
    static uint8_t is_overflown = false;

    if (length == 0)
    {
        sequence_stop(true);
        return is_overflown;
    }

    uint8_t result = sequence_push_packet(data, length);
    if (result == PUSH_OVERFLOW)
    {
        NRF_LOG_ERROR("buffer overflown\n");
        is_overflown = true;
        return is_overflown;
    }
    if (result == PUSH_FIRST_PACKET)
    {
        is_overflown = false;
        NRF_LOG_DEBUG("first packet\n");
        return is_overflown;
    }
    if (result == PUSH_FINAL_PACKET)
    {
        if (is_overflown)
        {
            NRF_LOG_ERROR("not starting sequence due to overflow\n");
            return is_overflown;
        }
        NRF_LOG_DEBUG("last packet\n");
        sequence_start();
    }
    return is_overflown;
}

void ble_gpio_asm_handle_input_change(uint32_t index, gpio_config_input_digital_t *config)
{
    sequence_handle_digital_input_update(index, config->state);
}

void ble_gpio_asm_on_connect(ble_evt_t *p_ble_evt)
{
    ble_gpio_asm_connection_handle = p_ble_evt->evt.gap_evt.conn_handle;
}

void ble_gpio_asm_on_disconnect(ble_evt_t *p_ble_evt)
{
    UNUSED_PARAMETER(p_ble_evt);
    ble_gpio_asm_connection_handle = BLE_CONN_HANDLE_INVALID;
}


void ble_gpio_asm_authorize_data_write(ble_gatts_evt_write_t *write_req)
{
    uint16_t status = BLE_GATT_STATUS_SUCCESS;

    ble_gatts_rw_authorize_reply_params_t authorize_params = {
        .type = BLE_GATTS_AUTHORIZE_TYPE_WRITE,
        .params.write = {
            .update = 1,
            .offset = 0,
            .len = write_req->len,
            .p_data = write_req->data}};

    if(ble_gpio_asm_handle_data_write(write_req->data, write_req->len)){
        status = BLE_GATT_STATUS_ATTERR_INVALID_ATT_VAL_LENGTH;
    }

    authorize_params.params.write.gatt_status = status;

    sd_ble_gatts_rw_authorize_reply(
        ble_gpio_asm_connection_handle,
        &authorize_params);
}

void ble_gpio_asm_on_authorize(ble_evt_t *p_ble_evt)
{
    ble_gatts_evt_rw_authorize_request_t *req = &(p_ble_evt
                                                      ->evt.gatts_evt
                                                      .params
                                                      .authorize_request);

    if (req->type == BLE_GATTS_AUTHORIZE_TYPE_READ)
    {
        /*
        uint16_t handle = req
                              ->request
                              .read
                              .handle;
        */
    }
    else if (req->type == BLE_GATTS_AUTHORIZE_TYPE_WRITE)
    {
        uint16_t handle = req
                              ->request
                              .write
                              .handle;

        if (handle == ble_gpio_asm_characteristic_data_handle)
        {
            ble_gpio_asm_authorize_data_write(&(req->request.write));
            return;
        }
        return;
    }
}


void ble_gpio_asm_handle_pin_digital_data(
    uint8_t *pin_data,
    uint32_t pin_data_length)
{

    uint32_t available_output_count = gpio_get_output_digital_pin_count();
    uint32_t sent_output_count = pin_data_length * 4;

    uint32_t parsed_output_count = MIN(available_output_count, sent_output_count);

    for (int index = 0; index < parsed_output_count; index++)
    {
        uint8_t output_bits = encoding_get_pin_bits(pin_data, pin_data_length, index);

        if (output_bits == 0b11)
        {
            // don't touch state, 0b11 meand ignore
            continue;
        }
        if (output_bits == 0b10)
        {
            // tri-state not supported
            continue;
        }
        uint8_t new_state = (output_bits == 0b01);
        if (new_state == gpio_get_output_digital_state(index))
        {
            continue;
        }
        gpio_write_output_digital_pin(index, new_state);
    }
}



void ble_gpio_asm_handle_pin_analog_data(
    uint32_t index,
    uint16_t duty_cycle
    )
{
    if(duty_cycle == 0xffff){
        return;
    }
    if(index > gpio_get_output_analog_pin_count()){
        NRF_LOG_ERROR("writing to unconfigured analog channel %i\n", index);
        return;
    }
    gpio_write_output_analog_pin_us(index, duty_cycle);
}

void ble_gpio_asm_on_ble_evt(ble_evt_t *p_ble_evt)
{
    switch (p_ble_evt->header.evt_id)
    {
    case BLE_GAP_EVT_CONNECTED:
        ble_gpio_asm_on_connect(p_ble_evt);
        break;

    case BLE_GAP_EVT_DISCONNECTED:
        ble_gpio_asm_on_disconnect(p_ble_evt);
        break;

    case BLE_GATTS_EVT_WRITE:
        // ble_gpio_asm_on_write(p_ble_evt);
        break;

    case BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST:
        ble_gpio_asm_on_authorize(p_ble_evt);
        break;

    default:
        // No implementation needed.
        break;
    }
}

void ble_gpio_asm_init(){
    ret_code_t err_code;

    ble_uuid128_t vs_uuid = {
        .uuid128 = UUID_GPIO_ASM_BASE
    };

    err_code = sd_ble_uuid_vs_add(&vs_uuid, &ble_gpio_asm_custom_uuid_type);
    APP_ERROR_CHECK(err_code);

    ble_uuid_t uuid_service = {
        .uuid = UUID_GPIO_ASM_SERVICE,
        .type = ble_gpio_asm_custom_uuid_type
    };

    err_code = sd_ble_gatts_service_add(
        BLE_GATTS_SRVC_TYPE_PRIMARY,
        &uuid_service,
        &ble_gpio_asm_service_handle
    );
    APP_ERROR_CHECK(err_code);

    err_code = ble_gpio_asm_characteristic_asm_data_add();
    APP_ERROR_CHECK(err_code);

    uint32_t output_digital_pin_count = gpio_get_output_digital_pin_count();
    uint32_t output_analog_pin_count = gpio_get_output_analog_pin_count();
    uint32_t input_digital_pin_count = gpio_get_input_digital_pin_count();

    sequence_init(
        encoding_get_byte_count_from_pins(output_digital_pin_count),
        encoding_get_byte_count_from_pins(input_digital_pin_count),
        output_analog_pin_count,
        ble_gpio_asm_handle_pin_digital_data,
        ble_gpio_asm_handle_pin_analog_data);
}