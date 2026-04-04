local SENSITIVITY = 0.5
local MOVEMENT_SPEED = 3.0
local FOREGROUND_COLOR = 0xFFED9564
local BACKGROUND_COLOR = 0xFF181818
local NEAR_PLANE = 0.1
local FAR_PLANE = 30.0

local MOUSE_NORMAL = 1
local MOUSE_DISABLED = 2

local mouse_mode = MOUSE_NORMAL
local camera = nil
local projection = nil
local draw_options = nil
local angle = 0.0
local last_dt = 1.0 / 60.0
local music = nil

local function set_mouse_mode(mode)
    mouse_mode = mode
    if mode == MOUSE_DISABLED then
        rgfw.show_mouse(false)
        rgfw.hold_mouse()
    else
        rgfw.show_mouse(true)
        rgfw.unhold_mouse()
    end
end

local function make_model_matrix(position, scale, rotation_degrees, axis)
    return hmm.mul_m4(
        hmm.translate(position),
        hmm.mul_m4(
            hmm.rotate_rh(rotation_degrees, axis),
            hmm.scale(scale)
        )
    )
end

local function append_draw_call(calls, asset, position, scale, rotation_degrees, axis, color, view)
    local model = make_model_matrix(position, scale, rotation_degrees, axis)
    local view_center = hmm.transform_point(view, position)

    calls[#calls + 1] = {
        asset = asset,
        model = model,
        color = color,
        view_depth = -view_center.z,
    }
end

local function draw_test_scene(view, teapot_rotation)
    local calls = {}

    append_draw_call(
        calls,
        "cube",
        hmm.vec3(0.0, -1.55, 0.0),
        hmm.vec3(8.0, 0.30, 8.0),
        0.0,
        hmm.vec3(0.0, 1.0, 0.0),
        0xFF3C4650,
        view
    )

    append_draw_call(
        calls,
        "cube",
        hmm.vec3(0.0, 0.1, -4.6),
        hmm.vec3(7.5, 3.0, 0.35),
        0.0,
        hmm.vec3(0.0, 1.0, 0.0),
        0xFF25313A,
        view
    )

    append_draw_call(
        calls,
        "cube",
        hmm.vec3(-3.1, -0.15, -1.6),
        hmm.vec3(0.8, 2.8, 0.8),
        0.0,
        hmm.vec3(0.0, 1.0, 0.0),
        0xFFC85A44,
        view
    )

    append_draw_call(
        calls,
        "cube",
        hmm.vec3(3.0, -0.35, -1.4),
        hmm.vec3(1.3, 1.9, 0.9),
        22.0,
        hmm.vec3(0.0, 1.0, 0.0),
        0xFF5E8E3E,
        view
    )

    append_draw_call(
        calls,
        "cube",
        hmm.vec3(-1.75, -0.95, 2.1),
        hmm.vec3(1.6, 0.55, 2.3),
        18.0,
        hmm.vec3(0.0, 1.0, 0.0),
        0xFF4979A8,
        view
    )

    append_draw_call(
        calls,
        "cube",
        hmm.vec3(2.1, -0.55, 1.8),
        hmm.vec3(2.7, 0.35, 0.8),
        -28.0,
        hmm.vec3(1.0, 0.0, 0.0),
        0xFF8A6BCE,
        view
    )

    append_draw_call(
        calls,
        "cube",
        hmm.vec3(0.0, -0.95, 0.0),
        hmm.vec3(1.8, 0.6, 1.8),
        0.0,
        hmm.vec3(0.0, 1.0, 0.0),
        0xFFD2B870,
        view
    )

    append_draw_call(
        calls,
        "utah_teapot",
        hmm.vec3(0.0, 0.1, 0.0),
        hmm.vec3(0.4, 0.4, 0.4),
        teapot_rotation,
        hmm.vec3(0.0, 1.0, 0.0),
        FOREGROUND_COLOR,
        view
    )

    table.sort(calls, function(a, b)
        return a.view_depth < b.view_depth
    end)

    for _, call in ipairs(calls) do
        core.draw_model(call.asset, call.model, view, projection, call.color, draw_options)
    end
end

function init()
    hello_world()

    do
        local ok, err = audio.init()
        assert(ok, err)
    end
    do
        local handle, err = audio.load_sound("assets/Duvet.mp3")
        assert(handle, err)
        music = handle
    end
    audio.set_looping(music, true)
    do
        local ok, err = audio.start(music)
        assert(ok, err)
    end

    camera = core.camera({0.0, 1.35, 8.0}, 8.0, 180.0)
    projection = hmm.perspective_rh_no(70.0, rgfw.get_width() / rgfw.get_height(), NEAR_PLANE, FAR_PLANE)
    draw_options = {
        near_plane = NEAR_PLANE,
        light_direction_world = hmm.vec3(0.0, -1.0, -1.0),
        ambient_strength = 0.15,
        diffuse_strength = 0.85,
        specular_strength = 0.35,
        shininess = 48.0,
        fog_color = BACKGROUND_COLOR,
        fog_power = 1.8,
    }

    assert(rgfw.get_width() > 0)
    assert(math.abs(hmm.len3(hmm.vec3(3, 4, 0)) - 5.0) < 0.001)
    assert(camera:view().type == "mat4")

    set_mouse_mode(MOUSE_DISABLED)
    rgfw.set_exit_key(rgfw.key_escape)
end

function update(dt)
    local forward_distance = 0.0
    local strafe_distance = 0.0
    local vertical_distance = 0.0
    local mouse_delta = rgfw.get_mouse_vector()
    local yaw_delta = 0.0
    local pitch_delta = 0.0

    if rgfw.is_key_pressed(rgfw.key_escape) then
        set_mouse_mode(MOUSE_NORMAL)
        rgfw.close()
    end

    if rgfw.is_mouse_pressed(rgfw.mouse_left) and mouse_mode == MOUSE_NORMAL then
        set_mouse_mode(MOUSE_DISABLED)
    end

    if rgfw.is_key_down(rgfw.key_w) then forward_distance = forward_distance + MOVEMENT_SPEED * dt end
    if rgfw.is_key_down(rgfw.key_s) then forward_distance = forward_distance - MOVEMENT_SPEED * dt end
    if rgfw.is_key_down(rgfw.key_a) then strafe_distance = strafe_distance - MOVEMENT_SPEED * dt end
    if rgfw.is_key_down(rgfw.key_d) then strafe_distance = strafe_distance + MOVEMENT_SPEED * dt end
    if rgfw.is_key_down(rgfw.key_space) then vertical_distance = vertical_distance + MOVEMENT_SPEED * dt end
    if rgfw.is_key_down(rgfw.key_e) then vertical_distance = vertical_distance + MOVEMENT_SPEED * dt end
    if rgfw.is_key_down(rgfw.key_q) then vertical_distance = vertical_distance - MOVEMENT_SPEED * dt end
    if rgfw.is_key_down(rgfw.key_shift_left) or rgfw.is_key_down(rgfw.key_shift_right) then
        vertical_distance = vertical_distance - MOVEMENT_SPEED * dt
    end

    if mouse_mode == MOUSE_DISABLED then
        yaw_delta = yaw_delta - mouse_delta.x * SENSITIVITY
        pitch_delta = pitch_delta + mouse_delta.y * SENSITIVITY
    end

    camera:look(yaw_delta, pitch_delta, -89.9, 89.9)
    camera:move(forward_distance, strafe_distance, vertical_distance)

    last_dt = dt
    angle = angle + 90.0 * dt
    if angle >= 360.0 then
        angle = angle - 360.0
    end
end

function draw()
    local view = camera:view()
    local fps = math.floor(1.0 / math.max(0.0001, last_dt))

    core.begin_frame(BACKGROUND_COLOR, 1.0)
    draw_test_scene(view, angle)

    core.draw_text("Iosevka-Regular", string.format("fps: %d", fps), 8, 8, 1, 0xFFFFFFFF)
    core.draw_text("Iosevka-Regular", "WASD move, mouse look, Q/E rise, Esc exits", 8, 40, 1, 0xFFFFFFFF)
end

function quit()
    if music ~= nil then
        audio.stop(music)
        audio.unload_sound(music)
        music = nil
    end

    if audio.is_ready() then
        audio.shutdown()
    end
end
