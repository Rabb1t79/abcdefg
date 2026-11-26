#pragma once
void ResetBindings() {
    functions.clear();
    missing_functions.clear();
}
uintptr_t Find(const Reader& r, const std::string& path) {
    if (!objects || !objects->find_exact) return 0;
    auto& h = functions[path];
    const auto old = Resolve(r, objects, h);
    if (old) return old;
    if (Clock::now() < missing_functions[path]) return 0;
    if (objects->find_exact(objects->user, StringView(path), &h).code != 0) {
        h = {};
        missing_functions[path] = Clock::now() + std::chrono::seconds(5);
        return 0;
    }
    const auto found = Resolve(r, objects, h);
    if (!found) missing_functions[path] = Clock::now() + std::chrono::seconds(5);
    return found;
}
uintptr_t Fn(const Reader& r, const char* package, const char* owner, const char* name, int count, int size) {
    const auto f = Find(r, std::string("/Script/") + package + "." + owner + "." + name);
    return f && r.Function(f, name, static_cast<uint8_t>(count), static_cast<uint16_t>(size)) ? f : 0;
}
int StructField(const Reader& r, uintptr_t cls, const char* name, int size, std::string_view kind) {
    for (int depth = 0; cls && depth < 32; ++depth) {
        uintptr_t p{};
        if (!r.Read(cls + tycoon::profile::snapshot.layout.PropertyLink, p)) break;
        for (int i = 0; p && i < 2048; ++i) {
            if (r.NameAt(p + tycoon::profile::snapshot.layout.FieldName) == name) {
                int bytes{}, offset{};
                if (r.Kind(p, kind) && r.Read(p + tycoon::profile::snapshot.layout.PropertySize, bytes) && bytes == size &&
                    r.Read(p + tycoon::profile::snapshot.layout.PropertyOffset, offset) && offset >= 0 && offset < 0x20000)
                    return offset;
                return -1;
            }
            if (!r.Read(p + tycoon::profile::snapshot.layout.PropertyNext, p)) break;
        }
        if (!r.Read(cls + tycoon::profile::snapshot.layout.Super, cls)) break;
    }
    return -1;
}
int Field(const Reader& r, uintptr_t object, const char* name, int size, std::string_view kind) {
    uintptr_t cls{};
    if (!r.Read(object + tycoon::profile::snapshot.layout.ObjectClass, cls)) return -1;
    return StructField(r, cls, name, size, kind);
}
uintptr_t Child(const Reader& r, uintptr_t parent, const char* field) {
    const auto offset = Field(r, parent, field, sizeof(uintptr_t), "ObjectProperty");
    uintptr_t value{};
    if (offset >= 0) r.Read(parent + offset, value);
    return value;
}
uintptr_t World(const Reader& r) {
    if (!world_slot) return 0;
    uintptr_t value{};
    return r.Read(world_slot, value) && r.IsA(value, "World") ? value : 0;
}
