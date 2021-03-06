#---
#--- Library
#---

umc_version=0.2

export umcRoot="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && cd .. && pwd )"

export toolsBin=$umcRoot/bin

#---------------------------------------------------------------------------------------
#--- call cfg scripts
#---------------------------------------------------------------------------------------
# configure user params
. $umcRoot/etc/umc.cfg

# configure global params
. $toolsBin/global.cfg

#---------------------------------------------------------------------------------------
#--- check required python modules
#---------------------------------------------------------------------------------------
if python $toolsBin/checkModule.py pyyaml; then
    echo "Note: pyyaml module not available. Installing in user space..."
    oldDir=$PWD
    cd /tmp
    unzip -o $umcRoot/varia/pyyaml-master.zip >/dev/null 2>&1
    cd pyyaml-master
    python setup.py install --user >/dev/null 2>&1
    cd ..
    rm -rf pyyaml-master
    cd $oldDir
fi

#---------------------------------------------------------------------------------------
#--- # ATTENTION!
#--- Do not use buffered!
#--- Timestamps added to data rows will contain not correct information. 
#--- It will be moment of buffer flush not a proper moment of collecting data.
#---------------------------------------------------------------------------------------
export BUFFERED=no

function availableSensors {
    cat $umcRoot/bin/global.cfg | grep "_layer=" | cut -f1 -d'=' | cut -f2 -d' ' | cut -f1 -d'_'
}

function usage {

    cat <<EOF
Universal Metrics Collector. Collects system monitoring data and presents in CSV format.

Usage: umc [sensors|test|help|-V] [SENSOR collect delay count] 

    Sensor commands:
        SENSOR.......sensor to collect data

        collect......collect data
        delay........delay in seconds between data collections
        count........number of data collections

    General commands:
        sensors......list available sensors
        test.........perform simple test

        help.........this help
        -V...........get version information

Example:
    umc free collect 5 2
    datetime,timezone,timestamp,system,source,total,used,free,shared,buffers,cached,usedNoBuffersCache,freePlusBuffersCache,SwapTotal,SwapUsed,SwapFree
    2018-01-11 05:00:55,-0800,1515675655,soabpm-vm.site,free,5607456,5123548,483908,0,24384,1061536,4037628,1569828,4128764,163332,3965432
    2018-01-11 05:01:00,-0800,1515675660,soabpm-vm.site,free,5607456,5134916,472540,0,24400,1062808,4047708,1559748,4128764,163284,3965480

EOF

}

function version {
    cat <<EOF
umc version $umc_version
rstyczynski@gmail.com, https://github.com/rstyczynski/umc
EOF
}

# main wrapper over other methods. User should use this command to use umc
function umc {
    export ALL_ARGS=$@

    if [ -z $1 ]; then
        sensor=none
    else
        sensor=$1; shift
    fi

    if [ $sensor = help -o $sensor = -V -o $sensor = test -o $sensor = sensors ]; then
        command=$sensor
    else
        command=sensor_$1; shift
        delay=$1; shift
        count=$1; shift
        params=$@
    fi
     
    case $command in
        sensor_help)
            invoke $sensor help
        ;;
        
        sensor_collect)
            invoke $sensor $delay $count $params
        ;;
        
        sensors)
            echo $(availableSensors)
        ;;
        
        test)
            umcTestRun
        ;;
        
        -V)
            version
        ;;
        
        help) 
            usage
            return 0
        ;;
        
        *)
            usage
            return 1
        ;;
    esac
}

function getLayerDirectories {
  local layer=$1
  
  layer_version_major=$(eval "echo $(echo \$$layer\_version_major)")
  layer_version_minor=$(eval "echo $(echo \$$layer\_version_minor)")
  layer_version_patch=$(eval "echo $(echo \$$layer\_version_patch)")
  layer_version_specific=$(eval "echo $(echo \$$layer\_version_specific)")
  
  if [ ! "$layer/$layer_version_major/$layer_version_minor/$layer_version_patch/$layer_version_specific" = "$layer////" ]; then
    echo "$layer/$layer_version_major/$layer_version_minor/$layer_version_patch/$layer_version_specific"
  fi
  if [ ! "$layer/$layer_version_major/$layer_version_minor/$layer_version_patch" = "$layer///" ]; then
    echo "$layer/$layer_version_major/$layer_version_minor/$layer_version_patch"
  fi
  if [ ! "$layer/$layer_version_major/$layer_version_minor" = "$layer//" ]; then
    echo "$layer/$layer_version_major/$layer_version_minor"
  fi
  if [ ! "$layer/$layer_version_major" = "$layer/" ]; then
    echo "$layer/$layer_version_major"
  fi
  echo "$layer"
  
}

function getDirectories {
  layer=$1
  
  for directoryRoot in ""; do
      for directoryLinux in $(getLayerDirectories linux); do
        if [ ! $layer = "linux" ]; then
            for directoryJava in $(getLayerDirectories java); do
                if [ ! $layer = "java" ]; then
                    for directoryWLS in $(getLayerDirectories wls); do
                        if [ ! $layer = "wls" ]; then
                            for directorySOA in $(getLayerDirectories soa); do
                                echo -n "$umcRoot/tools"
                                echo $directoryRoot/$directoryLinux/$directoryJava/$directoryWLS/$directorySOA
                            done
                        else
                            echo -n "$umcRoot/tools"
                            echo $directoryRoot/$directoryLinux/$directoryJava/$directoryWLS
                        fi
                    done
                else
                    echo -n "$umcRoot/tools"
                    echo $directoryRoot/$directoryLinux/$directoryJava
                fi
            done
        else
            echo -n "$umcRoot/tools"
            echo $directoryRoot/$directoryLinux
        fi
      done
    done
}

function locateToolExecDir {
  cmd=$1
  cmd_layer=$(eval "echo $(echo \$$cmd\_layer)")
  cmd_package=$(eval "echo $(echo \$$cmd\_package)")
  
  unset toolExecDir
  
  directories=$(getDirectories $cmd_layer);
  for directory in $directories; do
    if [ -f $directory/$cmd ]; then
        toolExecDir=$directory
        return 0
    fi
    if [ ! -z $cmd_package ]; then
        if [ -f $directory/$cmd_package/$cmd ]; then
            toolExecDir=$directory/$cmd_package
            return 0
        fi
    fi
  done
  
  if [ -z $toolExecDir ]; then
    echo "Error! Reason: utility not recognized as supported tool."
    echo "Available versions:"
    find $umcRoot/tools -name $cmd -type f | sed 's/^/--- /g'
    echo "Your version:"
    cmd_version=$(eval "echo $(echo \$$cmd\_package)")
    echo "--- $cmd_version"
    return 3
  fi
}


function cfgInfoFile {

  # reset all env settings
  unset UMC_PROBE_META_EXT
  unset UMC_SENSOR_HELP
  
  #configure enronment for tool. *setenv is a part of binaries. It's not a configuration file.
  # e.g. set UMC_SENSOR_HEADER  
  if [ -f $toolExecDir/$cmd.setenv ]; then
    . $toolExecDir/$cmd.setenv $ALL_ARGS
  fi

  if [ -z "$UMC_PROBE_META_EXT" ]; then
    probeInfo=$toolExecDir/$cmd.info
    probeYAMLRoot=$cmd
  else
    probeInfo=$toolExecDir/$cmd\_$UMC_PROBE_META_EXT.info
    probeYAMLRoot=$cmd\_$UMC_PROBE_META_EXT
  fi
}

function assertInvoke {
  toolCmd=$1

  unset availabilityMethod 
  
  cfgInfoFile 

  availabilityMethod=$($toolsBin/getCfg.py $probeInfo $probeYAMLRoot.availability.method)
  if [ $? -ne 0 ]; then
    availabilityMethod="None"
  fi
  
  if [ "$availabilityMethod" = "None" ]; then
    $toolCmd 2>/dev/null 1>/dev/null
    if [ $? -eq 127 ]; then
      echo "Error! Reason: utility not installed."
      return 2
    fi
  fi

  if [ "$availabilityMethod" = "command" ]; then
    command=$($toolsBin/getCfg.py $probeInfo $probeYAMLRoot.availability.directive)
    $command 2>/dev/null 1>/dev/null
    if [ $? -eq 127 ]; then
      echo "Error! Reason: utility not installed."
      return 2
    fi
  fi

  if [ "$availabilityMethod" = "file" ]; then
    file=$($toolsBin/getCfg.py $probeInfo $probeYAMLRoot.availability.directive)
    if [ ! -f $file ]; then
      echo "Error! Reason: data file not available."
      return 2
    fi 
  fi

  if [ "$availabilityMethod" = "env" ]; then
    envVariable=$($toolsBin/getCfg.py $probeInfo $probeYAMLRoot.availability.directive)
    if [ -z $envVariable ]; then
      echo "Error! Reason: required variable not available."
      return 2
    fi 
  fi

}

function cfgBuffered {

 if [ "$BUFFERED" = "yes" ]; then
   export sedBUFFER=""
   export grepBUFFER=""
   export perlBUFFER=""
   export addTimestampBUFFER=""
   export joinlinesBUFFER=""
  else
   export sedBUFFER="-u"
   export grepBUFFER="--line-buffered"
   export perlBUFFER="\$|=1"
   export addTimestampBUFFER="-notbuffered"
   export joinlinesBUFFER="-notbuffered"
  fi
}

function string2value {
    optionsString=$1

    for element in $optionsString; do
      if [[ $element == '$'* ]]; then
        value=$(eval  echo $element)
        #echo $element, $value
        sedCmd="s#$element#$value#g"
        #echo $sedCmd
        optionsString=$(echo $optionsString | sed "$sedCmd")
      fi
    done
    
    echo $optionsString

}

function invoke {

  unset DEBUG
  if [ "$1" = "DEBUG" ]; then
        export uosmcDEBUG=DEBUG
        shift
  fi

  export LC_CTYPE=en_US.UTF-8
  export LC_ALL=en_US.UTF-8

  export cmd=$1
  shift

  #setBuffered or not buffered operation
  cfgBuffered

  #locate tool definition directory
  locateToolExecDir $cmd
  if [ $? -eq 3 ]; then
    return 3
  fi

  #check if tool is installed on this platform
  assertInvoke $cmd
  if [ $? -eq 2 ]; then
    return 2
  fi

  interval=$1
  count=$2
  #check if looping is requited
  unset loop
  
  cfgInfoFile 

  loop=$($toolsBin/getCfg.py $probeInfo $probeYAMLRoot.loop.method)
  if [ $? -ne 0 ]; then
    loop="None"
  fi
  
  if [ "$loop" = "external" ]; then
        shift 2  
  elif [ "$loop" = "options" ]; then
        shift 2
        loop_string=$($toolsBin/getCfg.py $probeInfo $probeYAMLRoot.loop.directive)
        #convert strong to values
        loop_options=$(string2value "$loop_string")
  fi


  #hostname
  hostname=$(hostname)


#  # reset all env settings
#  unset UMC_PROBE_META_EXT
#  unset UMC_SENSOR_HELP

#  #configure enronment for tool. *setenv is a part of binaries. It's not a configuration file.
#  # e.g. set UMC_SENSOR_HEADER  
#  if [ -f $toolExecDir/$cmd.setenv ]; then
#    . $toolExecDir/$cmd.setenv $@
#  fi

  #TODO implement proper handler for options
  if [ $interval = "help" ]; then
    $toolExecDir/$cmd $UMC_SENSOR_HELP
    return 1
  fi

  #TODO: move global cfg to YAML
  #global header prefix
  export CSVheader=$(cat $umcRoot/etc/global.header | tr -d '\n')
  CSVheader="$CSVheader$CSVdelimiter"

  # tool header is available in $cmd.info
  CSVheader="$CSVheader$($toolsBin/getCfg.py $probeInfo $probeYAMLRoot.header)"
 
  #TODO pass header to log director 
  echo $CSVheader

  #add timestamp
  timestampMethod=$($toolsBin/getCfg.py $probeInfo $probeYAMLRoot.timestamp.method)
  if [ $? -eq 0 ]; then
    timestampDirective=$($toolsBin/getCfg.py $probeInfo $probeYAMLRoot.timestamp.directive)
  else
    timestampMethod=external
    timestampDirective=""
  fi
  

  #run the tool
  if [ "$loop" = "external" ]; then
    if [ "$timestampMethod" = "internal" ]; then
        $toolsBin/timedExec.sh $interval $count $uosmcDEBUG $toolExecDir/$cmd $timestampDirective $@
    else
        $toolsBin/timedExec.sh $interval $count $uosmcDEBUG $toolExecDir/$cmd $@ \
        | perl -ne "$perlBUFFER; print \"$hostname,$cmd,\$_\";" \
        | $toolsBin/addTimestamp.pl $addTimestampBUFFER -timedelimiter=" " -delimiter=$CSVdelimiter
    fi
  elif [ "$loop" = "options" ]; then
    if [ "$timestampMethod" = "internal" ]; then
        $toolExecDir/$cmd $loop_options $timestampDirective $@
    else
        $toolExecDir/$cmd $loop_options $@ \
        | perl -ne "$perlBUFFER; print \"$hostname,$cmd,\$_\";" \
        | $toolsBin/addTimestamp.pl $addTimestampBUFFER -timedelimiter=" " -delimiter=$CSVdelimiter
    fi
  else
    if [ "$timestampMethod" = "internal" ]; then
        $toolExecDir/$cmd $timestampDirective $@ 
    else
        $toolExecDir/$cmd $@ \
        | perl -ne "$perlBUFFER; print \"$hostname,$cmd,\$_\";" \
        | $toolsBin/addTimestamp.pl $addTimestampBUFFER -timedelimiter=" " -delimiter=$CSVdelimiter
    fi
  fi
}

function testCompatibility {
  toolCmd=$1
  toolExecDir=$2

  unset thisHeader
  unset systemHeader

  echo -n Testing compatibility of $2 with $1 ...

  #check if tool is installed on this platform
  assertInvoke $1
  if [ $? -eq 2 ]; then
    return 2
  fi

  #check if directory is available
  if [ ! -f $toolExecDir/$toolCmd ]; then
    echo "Error! Reason: The tool not found in given directory."
    return 3
  fi
  
    # reset all env settings
  unset UMC_PROBE_META_EXT
  unset UMC_SENSOR_HELP

  #configure enronment for tool. *setenv is a part of binaries. It's not a configuration file.
  # e.g. set UMC_SENSOR_HEADER  
  if [ -f $toolExecDir/$cmd.setenv ]; then
    . $toolExecDir/$cmd.setenv $@
  fi

  # tool header is available in $cmd.info
  if [ -z "$UMC_PROBE_META_EXT" ]; then
    rawHeaderMethod=$($toolsBin/getCfg.py $toolExecDir/$cmd.info $cmd.rawheader.method)
    rawHeaderDirective=$($toolsBin/getCfg.py $toolExecDir/$cmd.info $cmd.rawheader.directive)
    rawHeader=$($toolsBin/getCfg.py $toolExecDir/$cmd.info $cmd.rawheader.expected)
  else
    rawHeaderDirective=$($toolsBin/getCfg.py $toolExecDir/$cmd\_$UMC_PROBE_META_EXT.info $cmd\_$UMC_PROBE_META_EXT.rawheader.directive)
    rawHeader=$($toolsBin/getCfg.py $toolExecDir/$cmd\_$UMC_PROBE_META_EXT.info $cmd\_$UMC_PROBE_META_EXT.rawheader.expected)
  fi
  
  if [[ "$rawHeaderMethod" == "line" ]]; then
    systemHeader=$($toolCmd | sed -n "$rawHeaderDirective"p)
    if [ "$rawHeader" = "$systemHeader" ]; then
      echo OK
      #reportCompatibilityResult $toolCmd Success $toolExecDir
      return 0
    fi
  fi
  
  if [[ "$rawHeaderMethod" == "command" ]]; then
    systemHeader=$(eval $rawHeaderDirective)
    if [ "$rawHeader" = "$systemHeader" ]; then
      echo OK
      #reportCompatibilityResult $toolCmd Success $toolExecDir
      return 0
    fi
  fi
  
  if [[ "$rawHeaderMethod" == "script" ]]; then
    systemHeader=$(. $toolExecDir/$rawHeaderDirective)
    if [ "$rawHeader" = "$systemHeader" ]; then
      echo OK
      #reportCompatibilityResult $toolCmd Success $toolExecDir
      return 0
    fi
  fi

  echo "Error! Reason: different header"
  echo "Reported header: $systemHeader"
  echo "Expected header: $rawHeader"
  #reportCompatibilityResult $toolCmd Failure $toolExecDir
  return 1
}


#---
#--- Test Run
#---
function umcTestRun { 
 for cmd in $(availableSensors); do
 #for cmd in vmstat; do
    echo -n $cmd: 
    
    locateToolExecDir $cmd
    testCompatibility $cmd $toolExecDir
    
    invoke $cmd 1 1 >/dev/null
    if [ $? -ne 0 ]; then
        echo Error
        locateToolExecDir $cmd
    fi
 done
}

echo Universal Metrics Collector initialized.

