#/bin/sh
# wrapper script that will actually execute adb shell commands on the target.

/bin/sh -c "$*"
EXITCODE=$?
echo ""
echo "EXITCODE=$EXITCODE"

