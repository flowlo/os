#!/bin/sh

assert()
{
	out1=/tmp/output-$$.1.txt
	out2=/tmp/output-$$.2.txt

	$1 2>/dev/null > $out1
	echo -n $2 > $out2

	diff -q >/dev/null $out1 $out2

	if [ $? != 0 ]
	then
		echo "FAIL!"
		diff $out1 $out2
	else
		echo "OK!"
	fi

	rm $out1 $out2
}

reset()
{
	./svctl -d 0 || echo 'Error deleting secvault!'
	password=$(< /dev/urandom tr -dc _A-Z-a-z-0-9 | head -c${1:-10}; echo -n)
	echo $password | ./svctl -c 10 0
}

reset
echo -n "12345" > /tmp/sv_data0
assert "cat /tmp/sv_data0" '12345'
echo "Simple cat."

reset
echo -n "hallo" | dd of=/tmp/sv_data0 bs=1 count=5 2> /dev/null 1> /dev/null
assert "dd if=/tmp/sv_data0" 'hallo'
echo "write 5 bytes with dd (bs=1), read with cat"

reset
echo -n "hallo" | dd of=/tmp/sv_data0 bs=3 count=5 2> /dev/null 1> /dev/null
assert "dd if=/tmp/sv_data0" 'hallo'
echo "write with block size 3"

reset
echo -n "thisistoooooooooooolong" | dd of=/tmp/sv_data0 bs=1 count=100 2> /dev/null 1> /dev/null
assert "dd if=/tmp/sv_data0 bs=3" 'thisistooo'
echo 'Wrote too much to device.'

reset
echo -n "0123456789" | dd of=/tmp/sv_data0 bs=1 count=10 2> /dev/null 1> /dev/null
assert "dd if=/tmp/sv_data0 bs=100" '0123456789'
echo 'Read larger than device.' "read a bigger block than size of device"

reset
echo -n "012345" | dd of=/tmp/sv_data0 bs=1 count=6 2> /dev/null 1> /dev/null
assert "dd if=/tmp/sv_data0 bs=9" '012345'
echo 'Read more than available.'

reset
echo -n "hallo" > /tmp/sv_data0
echo -n "XX" >> /tmp/sv_data0
assert "dd if=/tmp/sv_data0 bs=1" 'halloXX'
echo 'Writing twice.'

./svctl -d 1 || echo 'Error deleting secvault!'
echo pwgen | ./svctl -c 1048576 1 || echo "ERROR creating secvault"

dd if=/dev/urandom bs=1024 count=1024 | base64 | dd of=/tmp/sv_data1 bs=1024 count=1024
dd if=/dev/random of=/tmp/sv_data1 bs=1024 count=1024 2> /dev/null 1> /dev/null
echo "Spawning 50 processes ..."

for i in {1..50}
do
	 dd if=/tmp/sv_data1 bs=1024 2> /dev/null 1> /dev/null &
done

wait

./svctl -d 0
