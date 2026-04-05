local ROUND_TIME = 30.0
local TARGET_COUNT = 3
local TARGET_RADIUS = 0.45
local SENSITIVITY = 0.1
local NEAR_PLANE = 0.1
local FAR_PLANE = 40.0

local MOUSE_NORMAL = 1
local MOUSE_DISABLED = 2

local BACKGROUND_COLOR = 0xFF181818
local FLOOR_COLOR = 0xFF3A4248
local PLATFORM_COLOR = 0xFF56697A
local TARGET_COLOR = 0xFF1E2CE8
local TEXT_COLOR = 0xFFFFFFFF
local CROSSHAIR_COLOR = 0xFFFFFFFF

local mouse_mode = MOUSE_NORMAL
local last_dt = 1.0 / 60.0
local time_remaining = ROUND_TIME
local score = 0
local game_over = false
local shot_sound = nil
local hit_sound = nil
local targets = {}

local spawn_points = {
    math.vec3(-4.8, 1.2, -5.5),
    math.vec3(0.0, 1.2, -5.5),
    math.vec3(4.8, 1.2, -5.5),
    math.vec3(-4.0, 2.5, -9.0),
    math.vec3(0.0, 2.7, -10.0),
    math.vec3(4.0, 2.5, -9.0),
    math.vec3(-5.2, 3.9, -13.0),
    math.vec3(0.0, 4.2, -14.5),
    math.vec3(5.2, 3.9, -13.0),
}

local arena_blocks = {
    {asset = "cube", position = math.vec3(0.0, -1.3, -4.5), scale = math.vec3(14.0, 0.4, 16.0), rotation = 0.0, axis = math.vec3(0.0, 1.0, 0.0), color = FLOOR_COLOR},
    {asset = "cube", position = math.vec3(0.0, 0.4, -8.2), scale = math.vec3(2.4, 0.25, 1.2), rotation = 0.0, axis = math.vec3(0.0, 1.0, 0.0), color = PLATFORM_COLOR},
    {asset = "cube", position = math.vec3(-4.3, 1.8, -11.8), scale = math.vec3(1.8, 0.25, 1.2), rotation = 15.0, axis = math.vec3(0.0, 1.0, 0.0), color = PLATFORM_COLOR},
    {asset = "cube", position = math.vec3(4.3, 1.8, -11.8), scale = math.vec3(1.8, 0.25, 1.2), rotation = -15.0, axis = math.vec3(0.0, 1.0, 0.0), color = PLATFORM_COLOR},
}

local function set_mouse_mode(mode)
    mouse_mode = mode
    if mode == MOUSE_DISABLED then
        window.show_mouse(false)
        window.hold_mouse()
    else
        window.show_mouse(true)
        window.unhold_mouse()
    end
end

local function make_model_matrix(position, scale, rotation_degrees, axis)
    return math.mul_m4(
        math.translate(position),
        math.mul_m4(
            math.rotate_rh(rotation_degrees, axis),
            math.scale(scale)
        )
    )
end

local function append_draw_call(calls, asset, position, scale, rotation_degrees, axis, color, view)
    local model = make_model_matrix(position, scale, rotation_degrees, axis)
    local view_center = math.transform_point(view, position)

    calls[#calls + 1] = {
        asset = asset,
        model = model,
        color = color,
        view_depth = -view_center.z,
    }
end

local function is_spawn_point_used(index)
    for _, target in ipairs(targets) do
        if target.spawn_index == index then
            return true
        end
    end
    return false
end

local function choose_spawn_index(excluded_index)
    local options = {}

    for index = 1, #spawn_points do
        if not is_spawn_point_used(index) and index ~= excluded_index then
            options[#options + 1] = index
        end
    end

    if #options == 0 then
        for index = 1, #spawn_points do
            if not is_spawn_point_used(index) then
                options[#options + 1] = index
            end
        end
    end

    if #options == 0 then
        return nil
    end

    return options[math.random(1, #options)]
end

local function target_draw_position(target)
    local bob_offset = math.sin(target.bob_phase) * 0.16
    return math.add3(target.position, math.vec3(0.0, bob_offset, 0.0))
end

local function spawn_target(excluded_index)
    local spawn_index = choose_spawn_index(excluded_index)
    if spawn_index == nil then
        return
    end

    targets[#targets + 1] = {
        spawn_index = spawn_index,
        position = spawn_points[spawn_index],
        rotation = math.random() * 360.0,
        bob_phase = math.random() * 6.28318,
    }
end

local function reset_round()
    time_remaining = ROUND_TIME
    score = 0
    game_over = false
    targets = {}

    for _ = 1, TARGET_COUNT do
        spawn_target(nil)
    end
end

local function replace_target(index)
    local removed = targets[index]
    table.remove(targets, index)
    spawn_target(removed and removed.spawn_index or nil)
end

local function play_sound(handle)
    local ok, err

    if handle == nil then
        return
    end

    ok, err = audio.stop(handle)
    assert(ok, err)
    ok, err = audio.seek(handle, 0.0)
    assert(ok, err)
    ok, err = audio.start(handle)
    assert(ok, err)
end

local function find_hit_target()
    local origin = camera:get_position()
    local direction = math.norm3(camera:forward())
    local best_index = nil
    local best_distance = nil

    for index, target in ipairs(targets) do
        local center = target_draw_position(target)
        local to_target = math.sub3(center, origin)
        local along = math.dot3(to_target, direction)

        if along > 0.0 then
            local closest_point = math.add3(origin, math.mul3f(direction, along))
            local miss_distance = math.len3(math.sub3(center, closest_point))

            if miss_distance <= TARGET_RADIUS then
                if best_distance == nil or along < best_distance then
                    best_index = index
                    best_distance = along
                end
            end
        end
    end

    return best_index
end

local function handle_shot()
    local hit_index = nil

    if game_over then
        return
    end

    play_sound(shot_sound)
    hit_index = find_hit_target()
    if hit_index ~= nil then
        score = score + 1
        play_sound(hit_sound)
        replace_target(hit_index)
    end
end

local function update_targets(dt)
    for _, target in ipairs(targets) do
        target.rotation = target.rotation + 85.0 * dt
        if target.rotation >= 360.0 then
            target.rotation = target.rotation - 360.0
        end
        target.bob_phase = target.bob_phase + dt * 2.0
    end
end

local function draw_scene(view)
    local calls = {}

    for _, block in ipairs(arena_blocks) do
        append_draw_call(
            calls,
            block.asset,
            block.position,
            block.scale,
            block.rotation,
            block.axis,
            block.color,
            view
        )
    end

    for _, target in ipairs(targets) do
        append_draw_call(
            calls,
            "sphere",
            target_draw_position(target),
            math.vec3(TARGET_RADIUS, TARGET_RADIUS, TARGET_RADIUS),
            target.rotation,
            math.vec3(0.0, 1.0, 0.0),
            TARGET_COLOR,
            view
        )
    end

    table.sort(calls, function(a, b)
        return a.view_depth < b.view_depth
    end)

    for _, call in ipairs(calls) do
        core.draw_model(call.asset, call.model, view, projection, call.color, draw_options)
    end
end

local function draw_crosshair()
    local cx = math.floor(window.get_width() / 2)
    local cy = math.floor(window.get_height() / 2)
    local arm = 8
    local gap = 3

    graphics.line(cx - arm, cy, cx - gap, cy, CROSSHAIR_COLOR)
    graphics.line(cx + gap, cy, cx + arm, cy, CROSSHAIR_COLOR)
    graphics.line(cx, cy - arm, cx, cy - gap, CROSSHAIR_COLOR)
    graphics.line(cx, cy + gap, cx, cy + arm, CROSSHAIR_COLOR)
end

local function draw_centered_text(text, y, scale, color)
    local width = #text * scale * 11
    local x = math.floor((window.get_width() - width) * 0.5)
    core.draw_text("Iosevka-Regular", text, x, y, scale, color)
end

local function draw_hud()
    local fps = math.floor(1.0 / math.max(0.0001, last_dt))

    core.draw_text("Iosevka-Regular", string.format("fps: %d", fps), 12, 12, 1, TEXT_COLOR)
    core.draw_text("Iosevka-Regular", string.format("time: %d", math.max(0, math.ceil(time_remaining))), 12, 36, 1, TEXT_COLOR)
    core.draw_text("Iosevka-Regular", string.format("score: %d", score), 12, 60, 1, TEXT_COLOR)
    core.draw_text("Iosevka-Regular", "mouse aim, left click shoot, esc exits", 12, 84, 1, TEXT_COLOR)
end

local function draw_game_over()
    local center_y = math.floor(window.get_height() * 0.35)

    draw_centered_text("TIME UP", center_y, 2, TEXT_COLOR)
    draw_centered_text(string.format("score: %d", score), center_y + 42, 2, TEXT_COLOR)
    draw_centered_text("press escape to exit", center_y + 92, 1, TEXT_COLOR)
end

function init()
    math.randomseed(os.time())

    do
        local ok, err = audio.init()
        assert(ok, err)
    end
    do
        local handle, err = audio.load_sound("gunshot")
        assert(handle, err)
        shot_sound = handle
    end
    do
        local handle, err = audio.load_sound("hit")
        assert(handle, err)
        hit_sound = handle
    end

    camera = core.camera(math.vec3(0.0, 1.6, 5.5), 0.0, 180.0)
    projection = math.perspective_rh_no(70.0, window.get_width() / window.get_height(), NEAR_PLANE, FAR_PLANE)
    draw_options = {
        near_plane = NEAR_PLANE,
        light_direction_world = math.vec3(0.0, -1.0, -1.0),
        ambient_strength = 0.18,
        diffuse_strength = 0.82,
        specular_strength = 0.25,
        shininess = 40.0,
        fog_color = BACKGROUND_COLOR,
        fog_power = 0.0,
        fog_start = FAR_PLANE,
        fog_end = FAR_PLANE,
    }

    reset_round()
    set_mouse_mode(MOUSE_DISABLED)
    window.set_exit_key(window.key_escape)
end

function update(dt)
    local mouse_delta = window.get_mouse_vector()
    local yaw_delta = 0.0
    local pitch_delta = 0.0

    last_dt = dt

    if window.is_key_pressed(window.key_escape) then
        set_mouse_mode(MOUSE_NORMAL)
        window.close()
    end

    if window.is_mouse_pressed(window.mouse_left) and mouse_mode == MOUSE_NORMAL then
        set_mouse_mode(MOUSE_DISABLED)
    end

    if mouse_mode == MOUSE_DISABLED then
        yaw_delta = yaw_delta - mouse_delta.x * SENSITIVITY
        pitch_delta = pitch_delta + mouse_delta.y * SENSITIVITY
    end

    camera:look(yaw_delta, pitch_delta, -70.0, 70.0)

    if window.is_mouse_pressed(window.mouse_left) then
        handle_shot()
    end

    if not game_over then
        update_targets(dt)
        time_remaining = time_remaining - dt
        if time_remaining <= 0.0 then
            time_remaining = 0.0
            game_over = true
        end
    end
end

function draw()
    local view = camera:view()

    core.begin_frame(BACKGROUND_COLOR, 1.0)
    draw_scene(view)
    draw_crosshair()
    draw_hud()

    if game_over then
        draw_game_over()
    end
end

function quit()
    if shot_sound ~= nil then
        audio.unload_sound(shot_sound)
        shot_sound = nil
    end

    if hit_sound ~= nil then
        audio.unload_sound(hit_sound)
        hit_sound = nil
    end

    if audio.is_ready() then
        audio.shutdown()
    end

    set_mouse_mode(MOUSE_NORMAL)
end
