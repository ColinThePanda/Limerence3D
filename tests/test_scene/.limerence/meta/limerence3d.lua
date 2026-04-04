---@meta

---@class LimerenceVec2
---@field x number
---@field y number

---@class LimerenceVec3
---@field x number
---@field y number
---@field z number

---@class LimerenceVec4
---@field x number
---@field y number
---@field z number
---@field w number

---@class LimerenceMat4
---@field type string
---@field [integer] number

---@class LimerenceDrawOptions
---@field near_plane? number
---@field lighting_enabled? boolean
---@field lighting_mode? 'none'|'flat'|'gouraud'
---@field light_direction_world? LimerenceVec3
---@field ambient_strength? number
---@field diffuse_strength? number
---@field specular_strength? number
---@field shininess? number
---@field occlusion_culling? boolean
---@field occlusion_test_step? integer
---@field fog_start? number
---@field fog_end? number
---@field fog_power? number
---@field fog_color? integer
---@field backface_culling? boolean

---@class LimerenceCamera
local LimerenceCamera = {}

function hello_world() end

---@return LimerenceVec3
function LimerenceCamera:get_position() end
---@param position LimerenceVec3
function LimerenceCamera:set_position(position) end
---@return number
function LimerenceCamera:get_pitch() end
---@param pitch number
function LimerenceCamera:set_pitch(pitch) end
---@return number
function LimerenceCamera:get_yaw() end
---@param yaw number
function LimerenceCamera:set_yaw(yaw) end
---@param yaw_delta number
---@param pitch_delta number
---@param pitch_min? number
---@param pitch_max? number
function LimerenceCamera:look(yaw_delta, pitch_delta, pitch_min, pitch_max) end
---@param forward_distance number
---@param strafe_distance number
---@param vertical_distance number
function LimerenceCamera:move(forward_distance, strafe_distance, vertical_distance) end
---@return LimerenceVec3
function LimerenceCamera:forward() end
---@return LimerenceVec3
function LimerenceCamera:flat_forward() end
---@return LimerenceVec3
function LimerenceCamera:right() end
---@return LimerenceMat4
function LimerenceCamera:view() end

---@class rgfw
---@field key_escape integer
---@field key_up integer
---@field key_down integer
---@field key_left integer
---@field key_right integer
---@field key_w integer
---@field key_a integer
---@field key_s integer
---@field key_d integer
---@field key_q integer
---@field key_e integer
---@field key_space integer
---@field key_shift_left integer
---@field key_shift_right integer
---@field mouse_left integer
---@field mouse_right integer
---@field mouse_middle integer
rgfw = {}

---@return integer
function rgfw.get_width() end
---@return integer
function rgfw.get_height() end
---@return boolean
function rgfw.should_close() end
function rgfw.close() end
---@param key integer
function rgfw.set_exit_key(key) end
---@param visible boolean
function rgfw.show_mouse(visible) end
---@return LimerenceVec2
function rgfw.get_mouse_vector() end
function rgfw.hold_mouse() end
function rgfw.unhold_mouse() end
---@param key integer
---@return boolean
function rgfw.is_key_pressed(key) end
---@param key integer
---@return boolean
function rgfw.is_key_released(key) end
---@param key integer
---@return boolean
function rgfw.is_key_down(key) end
---@param button integer
---@return boolean
function rgfw.is_mouse_pressed(button) end
---@param button integer
---@return boolean
function rgfw.is_mouse_released(button) end
---@param button integer
---@return boolean
function rgfw.is_mouse_down(button) end

---@class graphics
graphics = {}

---@param r integer
---@param g integer
---@param b integer
---@param a? integer
---@return integer
function graphics.rgba(r, g, b, a) end
---@return integer
function graphics.get_width() end
---@return integer
function graphics.get_height() end
---@param color integer
function graphics.clear(color) end
---@param x integer
---@param y integer
---@param w integer
---@param h integer
---@param color integer
function graphics.rect(x, y, w, h, color) end
---@param x integer
---@param y integer
---@param w integer
---@param h integer
---@param color integer
function graphics.frame(x, y, w, h, color) end
---@param x integer
---@param y integer
---@param r integer
---@param color integer
function graphics.circle(x, y, r, color) end
---@param x1 integer
---@param y1 integer
---@param x2 integer
---@param y2 integer
---@param color integer
function graphics.line(x1, y1, x2, y2, color) end
---@param x1 integer
---@param y1 integer
---@param x2 integer
---@param y2 integer
---@param x3 integer
---@param y3 integer
---@param color integer
function graphics.triangle(x1, y1, x2, y2, x3, y3, color) end
---@param x integer
---@param y integer
---@param color integer
function graphics.set_pixel(x, y, color) end

---@class core
core = {}

---@param clear_color integer
---@param clear_depth? number
function core.begin_frame(clear_color, clear_depth) end
---@param font string
---@param text string
---@param x integer
---@param y integer
---@param scale integer
---@param color integer
function core.draw_text(font, text, x, y, scale, color) end
---@param model string
---@param model_matrix LimerenceMat4
---@param view_matrix LimerenceMat4
---@param projection_matrix LimerenceMat4
---@param color integer
---@param options? LimerenceDrawOptions
function core.draw_model(model, model_matrix, view_matrix, projection_matrix, color, options) end
---@param position_or_x LimerenceVec3|table|number
---@param y_or_pitch? number
---@param z_or_yaw? number
---@param pitch? number
---@param yaw? number
---@return LimerenceCamera
function core.camera(position_or_x, y_or_pitch, z_or_yaw, pitch, yaw) end

---@class hmm
hmm = {}

---@param x number
---@param y number
---@return LimerenceVec2
function hmm.vec2(x, y) end
---@param x number
---@param y number
---@param z number
---@return LimerenceVec3
function hmm.vec3(x, y, z) end
---@param x number
---@param y number
---@param z number
---@param w number
---@return LimerenceVec4
function hmm.vec4(x, y, z, w) end
---@param a LimerenceVec3
---@param b LimerenceVec3
---@return LimerenceVec3
function hmm.add3(a, b) end
---@param a LimerenceVec3
---@param b LimerenceVec3
---@return LimerenceVec3
function hmm.sub3(a, b) end
---@param value LimerenceVec3
---@param scalar number
---@return LimerenceVec3
function hmm.mul3f(value, scalar) end
---@param a LimerenceVec3
---@param b LimerenceVec3
---@return number
function hmm.dot3(a, b) end
---@param a LimerenceVec3
---@param b LimerenceVec3
---@return LimerenceVec3
function hmm.cross(a, b) end
---@param value LimerenceVec3
---@return number
function hmm.len3(value) end
---@param value LimerenceVec3
---@return LimerenceVec3
function hmm.norm3(value) end
---@param a LimerenceVec3
---@param t number
---@param b LimerenceVec3
---@return LimerenceVec3
function hmm.lerp3(a, t, b) end
---@param value number
---@return number
function hmm.sin(value) end
---@param value number
---@return number
function hmm.cos(value) end
---@param min number
---@param value number
---@param max number
---@return number
function hmm.clamp(min, value, max) end
---@return LimerenceMat4
function hmm.identity4() end
---@param value LimerenceVec3
---@return LimerenceMat4
function hmm.translate(value) end
---@param value LimerenceVec3
---@return LimerenceMat4
function hmm.scale(value) end
---@param degrees number
---@param axis LimerenceVec3
---@return LimerenceMat4
function hmm.rotate_rh(degrees, axis) end
---@param eye LimerenceVec3
---@param center LimerenceVec3
---@param up LimerenceVec3
---@return LimerenceMat4
function hmm.look_at_rh(eye, center, up) end
---@param fov_degrees number
---@param aspect number
---@param near_plane number
---@param far_plane number
---@return LimerenceMat4
function hmm.perspective_rh_no(fov_degrees, aspect, near_plane, far_plane) end
---@param a LimerenceMat4
---@param b LimerenceMat4
---@return LimerenceMat4
function hmm.mul_m4(a, b) end
---@param matrix LimerenceMat4
---@param point LimerenceVec3
---@return LimerenceVec3
function hmm.transform_point(matrix, point) end
---@param matrix LimerenceMat4
---@param vector LimerenceVec3
---@return LimerenceVec3
function hmm.transform_vector(matrix, vector) end

---@class audio
audio = {}

---@return boolean ok, string? err
function audio.init() end
function audio.shutdown() end
---@return boolean
function audio.is_ready() end
---@param volume number
function audio.set_master_volume(volume) end
---@param path string
---@return boolean ok, string? err
function audio.play(path) end
---@param path string
---@return integer? handle, string? err
function audio.load_sound(path) end
---@param handle integer
function audio.unload_sound(handle) end
---@param handle integer
---@return boolean ok, string? err
function audio.start(handle) end
---@param handle integer
---@return boolean ok, string? err
function audio.stop(handle) end
---@param handle integer
---@param volume number
function audio.set_volume(handle, volume) end
---@param handle integer
---@param pitch number
function audio.set_pitch(handle, pitch) end
---@param handle integer
---@param pan number
function audio.set_pan(handle, pan) end
---@param handle integer
---@param looping boolean
function audio.set_looping(handle, looping) end
---@param handle integer
---@return boolean
function audio.is_playing(handle) end
---@param handle integer
---@return boolean
function audio.at_end(handle) end
---@param handle integer
---@param seconds number
---@return boolean ok, string? err
function audio.seek(handle, seconds) end

