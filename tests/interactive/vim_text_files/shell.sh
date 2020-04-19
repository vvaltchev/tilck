#!/bin/sh

dirs="bin etc usr lib"

for d in $dirs; do

   if ! [ -d /initrd/$d ]; then
      continue
   fi

   mkdir -p $d
   for x in /initrd/$d/*; do

      if [ -f $x ]; then

         ln -s $x $d/

      elif [ -d $x ]; then

         dname=$(basename $x)
         mkdir -p /$d/$dname

         for y in /initrd/$d/$dname/*; do

            if [ "$y" = "/initrd/$d/$dname/*" ]; then
               break
            fi

            ln -s $y /$d/$dname/
         done
      fi
   done
done
