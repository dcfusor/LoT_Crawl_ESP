#!/usr/bin/perl
use strict;
use FCGI; # for forms input
use CGI::Carp qw(fatalsToBrowser);
use DBI;
use Socket;
use IO::Select;

my $dsn = "DBI:mysql:host=localhost;database=OTInfra"; # data source name
my $tablename = "Minutes"; # testing for resting, OTData for the real thing
my $dbh;	# database connection handle
my $sql = "SELECT * FROM $tablename ORDER BY Epoch DESC LIMIT 1";
my $request = FCGI::Request; # object wrapping CGI request
my $rowref; # last row of DB, we hope
my $data; # any data submitted
my ($invalve,$dumpvalve);
my $valves; # composite number from DB
my $slavename = "ESP_B0F1E3";
my ($ESPipa, $ESPipn);
my $IOSel;
my $portno = 42042; # life, universe, everything
my $slavedest; 
my $msg;
my $maxtoread = 1024;
my $pipepath = "/tmp/valvecmd";
my $pipewrench; # file handle for pipe
my $valvecommand;


####################################
sub printlastval
{
 my ($instate, $dumpstate);

 $rowref = $dbh->selectrow_hashref($sql);
 print "Timestamp: $rowref->{Timestamp} Cistern gallons: $rowref->{WaterLevel}<br>\n";

 $valves = @$rowref{'Valves'};
 $instate = (($valves & 0x02) ? 'open' : 'closed');
 $dumpstate = (($valves & 0x01) ? 'open' : 'closed');
 print "Intake valve: $instate, Water in rate: $rowref->{WaterInRate}<br>\n";
 print "Dump valve:  $dumpstate, Water out rate: $rowref->{WaterOutRate}<br>\n"; 
# print "Water in rate: $rowref->{WaterInRate} Water out rate: $rowref->{WaterOutRate}<br>\n";
# print "Inlet valve: $instate  Dump valve: $dumpstate<br>\n";
}
####################################

sub makeform
{
 print "<FORM action=\"waterctl.cgi\" method=\"post\"><br>\n";
 print "<INPUT type=\"radio\" name = \"valve\" value = \"a\"> AutoFill<br><p>\n";
 print "<INPUT type=\"radio\" name = \"valve\" value = \"i\">Open Intake<br><p>\n";
 print "<INPUT type=\"radio\" name = \"valve\" value = \"o\">Open Dump<br><p>\n";
 print "<INPUT type=\"radio\" name = \"valve\" value = \"c\" checked>Close all<br><p>\n";
 print "<INPUT type =\"submit\" value =\"Set Valves\">\n";
 print "</FORM><br>\n";
}
####################################
sub startpage
{
 print "Content-type:text/html\n\n"; # get the ball rolling
 print <<EndOfHTML;
<html><head><title>Water Valves</title><meta http-equiv="refresh" content="60"></head>
<body style="background-image: url(../pix/angels.jpg);">
<h2>Current water status:</h2> 
EndOfHTML
} # there appears to be an automatic <p> after </h2>
####################################
sub endpage
{
 print '<a href="../index.html">Home</a><br> <p> </p>';
 print '<a href="../cgi/plotsnew.cgi"> Plots</a><br>';
 print "</body></html>\n";
}
####################################
sub doValves # only called if there's command data
{
	#@@@ @@@ change this to use pipe for commands instead
	# /tmp/valvecmd is pipe file
=pod
# get a socket
 my $msgsock;
 socket($msgsock,PF_INET,SOCK_DGRAM,getprotobyname("udp")) or loggit ("socket:");
 $IOSel = IO::Select->new($msgsock) or die "IOSel:$IOSel\n";   # quickie way to see if ready
 # get destination info
 $ESPipn = gethostbyname($slavename);
 $ESPipa = inet_ntoa($ESPipn); 
# print "I will use $ESPipa as the address for $slavename\n";
 $slavedest = sockaddr_in($portno, inet_aton($ESPipa)); # @@@ test later with ESPipn and no call
 # figure out what to send, aioc to ESP
 (undef,$msg) = split /=/, $data;
 send($msgsock,"$msg",0,$slavedest) or die("send:$msg\n"); # send request
 shutdown ($msgsock,2);
=cut
 open ($pipewrench, ">>", $pipepath) or print "couldn't open pipe $pipepath\n"; # destination for commands
 (undef,$msg) = split /=/, $data; # get command
# add fancy logic here later (water hammer stuff)
 if ($msg =~ /c/i) # case-insensitive c
 { # water hammer
  $valvecommand = "io3c1io3c1\n";
 } elsif ($msg=~ /i/i) {
  $valvecommand = "io5c1io5ci\n"; 
 } elsif ($msg =~ /a/i) {
  $valvecommand = "io5c1io5ca\n";
 } else {
 $valvecommand = $msg . "\n"; # doesn't flush print without nl!!!
 } 
print $pipewrench $valvecommand;
close $pipewrench;
 
}
############################### Main ########################
$dbh = DBI->connect($dsn,"DataAcq","Acq") or die "Cannot connect to db server\n"; # also, some free delay time
while ($request->Accept >= 0)
{
 { # closure for local $/
  local $/;
  $data = <>; # suck up any submit data
 }
 
 if ($data =~ m/valve=/)
 {
  doValves();
 }
 startpage(); # header, title and such
 printlastval();
 #print "<br>data:$data<br>\n";
 makeform();
 endpage();
} # while request

$dbh->disconnect(); # be polite
