void* _ZN15_C2FenceFactory15CreateSyncFenceEib(int fenceFd, _Bool validate);

void* _ZN15_C2FenceFactory15CreateSyncFenceEi(int fenceFd) {
    return _ZN15_C2FenceFactory15CreateSyncFenceEib(fenceFd, 1);
}
