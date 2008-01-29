#!/bin/sh

ORIG=sl_msr_new.3
LIST="sl_msr_free.3 sl_msr_parse.3 sl_msr_print.3 sl_msr_dsamprate.3 sl_msr_dnomsamprate.3 sl_msr_depochstime.3"
for link in $LIST ; do
    ln -s $ORIG $link
done

ORIG=sl_addstream.3
LIST="sl_setuniparams.3"
for link in $LIST ; do
    ln -s $ORIG $link
done

ORIG=sl_collect.3
LIST="sl_collect_nb.3 sl_terminate.3"
for link in $LIST ; do
    ln -s $ORIG $link
done

ORIG=sl_log.3
LIST="sl_log_r.3 sl_log_rl.3 sl_loginit.3 sl_loginit_r.3 sl_loginit_rl.3"
for link in $LIST ; do
    ln -s $ORIG $link
done

ORIG=sl_newslcd.3
LIST="sl_freeslcd.3"
for link in $LIST ; do
    ln -s $ORIG $link
done

ORIG=sl_packettype.3
LIST="sl_sequence.3"
for link in $LIST ; do
    ln -s $ORIG $link
done

ORIG=sl_read_streamlist.3
LIST="sl_parse_streamlist.3"
for link in $LIST ; do
    ln -s $ORIG $link
done

ORIG=sl_savestate.3
LIST="sl_recoverstate.3"
for link in $LIST ; do
    ln -s $ORIG $link
done

ORIG=sl_utils.3
LIST="sl_dtime.3 sl_doy2md.3 sl_strncpclean.3"
for link in $LIST ; do
    ln -s $ORIG $link
done
