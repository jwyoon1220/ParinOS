#!/bin/sh
# /test_java.sh — ParinOS Java 테스트 러너
#
# ParinOS 셸에서 실행하세요:
#   sh /test_java.sh
#
# 또는 QEMU headless 시리얼 출력으로:
#   qemu-system-i386 -nographic -serial stdio \
#     -net nic,model=ne2k_pci -net user,hostfwd=tcp::8080-:8080 \
#     -drive file=disk.img,format=raw,if=none,id=disk0 \
#     ...
#   (부팅 후 셸 프롬프트에서 sh /test_java.sh)

CLASSES=/classes
JVM=/bin/jvm
PASS=0
FAIL=0

run_test() {
    NAME=$1
    CLASS=$2
    echo ""
    echo ">>> Running $NAME..."
    $JVM "$CLASSES/$CLASS"
    RET=$?
    if [ $RET -eq 0 ]; then
        echo ">>> $NAME: OK (exit $RET)"
        PASS=$((PASS + 1))
    else
        echo ">>> $NAME: FAIL (exit $RET)"
        FAIL=$((FAIL + 1))
    fi
}

echo "========================================"
echo "  ParinOS Java Test Suite"
echo "========================================"

run_test "Test01: Hello World"       "Test01_HelloWorld"
run_test "Test02: Arithmetic"        "Test02_Arithmetic"
run_test "Test03: Control Flow"      "Test03_ControlFlow"
run_test "Test04: StringBuilder"     "Test04_StringBuilder"
run_test "Test05: Static Methods"    "Test05_StaticMethods"
run_test "Test06: Network (Socket)"  "Test06_Network"

echo ""
echo "========================================"
echo "  Results: PASS=$PASS  FAIL=$FAIL"
echo "========================================"
