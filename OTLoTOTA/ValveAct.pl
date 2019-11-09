#!/usr/bin/perl
use Modern::Perl;
use Socket;
use POSIX 'setsid';

my $pipewrench; # handle for pipe
my $pipepath = "/tmp/valvecmd";
my $data;
my @commands; # things to do
my $op;
my $msgsock;
my $slavename = "ESP_B0F1E3";
my $slavedest; 
my $pid;

#initialize stuff
unless (-p $pipepath) { # in case first run ever or since boot
# say "creating pipe";
 unlink $pipepath;
 system('mkfifo', '-m=0666', $pipepath) 
 && die "can't mknod $pipepath: $!";
}
open ($pipewrench, "+<$pipepath") # we block on this waiting for something to do
  or die "Couldn't open $pipepath: $!\n";
# get a socket
socket($msgsock,PF_INET,SOCK_DGRAM,getprotobyname("udp")); # or loggit ("socket:");
$slavedest = pack_sockaddr_in(42042,scalar gethostbyname($slavename)); # @@@ 

$pid = fork(); # we're going to detach from whoever started us
exit if $pid; # parent dies
POSIX::setsid();  #or say ("Can't start new session");
close (STDIN); # never use it anyway
close (STDOUT);
close (STDERR);
##################################################
############## main ##############################
##################################################
while (1)
{
 $data = <$pipewrench>;
# say "incoming:$data";
 chomp $data;
 push @commands,split //,$data;
# say "commands:@commands";
 foreach $op (@commands)
 { # do the operation
  if ($op =~ /\d/)  # it's a number
  {
  	sleep $op; # digits in stream specify a sleep time
  	next; # we're done with this one
  }
  # it's non numeric, pass to ESP...(hope it can handle crazy stuff)
#  say "op:$op"; # debug
  send($msgsock,"$op",0,$slavedest); #or die("send:$op\n"); # send request
 } # do each op
 @commands = (); # but clean up after
} # end while one
close($pipewrench); # probably don't need these if exiting anyway
shutdown ($msgsock,2);
############

