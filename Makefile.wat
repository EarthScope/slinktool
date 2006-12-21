
# Use 'wmake -f Makefile.wat'

all: .SYMBOLIC
	cd libslink
	wmake -f Makefile.wat
	cd ..\src
	wmake -f Makefile.wat
	cd ..

clean: .SYMBOLIC
	cd libslink
	wmake -f Makefile.wat clean
	cd ..\src
	wmake -f Makefile.wat clean
	cd ..
