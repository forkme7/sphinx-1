<?php

//
// $Id$
//

require ( "sphinxapi.php" );

//////////////////////
// parse command line
//////////////////////

// for very old PHP versions, like at my home test server
if ( is_array($argv) && !isset($_SERVER["argv"]) )
	$_SERVER["argv"] = $argv;
unset ( $_SERVER["argv"][0] );

// build query
if ( !is_array($_SERVER["argv"]) || empty($_SERVER["argv"]) )
	die ( "usage: php -f test.php [--any] <word [word [word [...]]]> [--group <group>] [-p <port>] [-i <index>]\n" );

$args = array();
foreach ( $_SERVER["argv"] as $arg )
	$args[] = $arg;

$q = "";
$mode = SPH_MATCH_ALL;
$groups = array();
$host = "localhost";
$port = 3312;
$index = "*";
$groupby = "";
for ( $i=0; $i<count($args); $i++ )
{
	$arg = $args[$i];

	if ( $arg=="-a" || $arg=="--any" )				$mode = SPH_MATCH_ANY;
	else if ( $arg=="-b" || $arg=="--boolean" )		$mode = SPH_MATCH_BOOLEAN;
	else if ( $arg=="-e" || $arg=="--extended" )	$mode = SPH_MATCH_EXTENDED;
	else if ( $arg=="-f" || $arg=="--filter" )		$groups[] = (int)$args[++$i];
	else if ( $arg=="-g" || $arg=="--groupby" )		$groupby = $args[++$i];
	else if ( $arg=="-h" || $arg=="--host" )		$host = $args[++$i];
	else if ( $arg=="-i" || $arg=="--index" )		$index = $args[++$i];
	else if ( $arg=="-p" || $arg=="--port" )		$port = (int)$args[++$i];
	else if ( $arg=="-s" || $arg=="--sortby" )		$sortby = $args[++$i];
	else
		$q .= $args[$i] . " ";
}

////////////
// do query
////////////

$cl = new SphinxClient ();
$cl->SetServer ( $host, $port );
$cl->SetWeights ( array ( 100, 1 ) );
$cl->SetMatchMode ( $mode );
if ( count($groups) )
	$cl->SetFilter ( "group_id", $groups );
if ( $groupby )
	$cl->SetGroupBy ( $groupby, SPH_GROUPBY_ATTR, false );
if ( $sortby )
	$cl->SetSortMode ( SPH_SORT_EXTENDED, $sortby );
$res = $cl->Query ( $q, $index );

////////////////
// print me out
////////////////

if ( $res===false )
{
	print "Query failed: " . $cl->GetLastError() . ".\n";

} else
{
	if ( $cl->GetLastWarning() )
		print "WARNING: " . $cl->GetLastWarning() . "\n\n";

	print "Query '$q' retrieved $res[total] of $res[total_found] matches in $res[time] sec.\n";
	print "Query stats:\n";
	if ( is_array($res["words"]) )
		foreach ( $res["words"] as $word => $info )
			print "    '$word' found $info[hits] times in $info[docs] documents\n";
	print "\n";

	if ( is_array($res["matches"]) )
	{
		$n = 1;
		print "Matches:\n";
		foreach ( $res["matches"] as $doc => $docinfo )
		{
			print "$n. doc_id=$doc, weight=$docinfo[weight]";
			foreach ( $res["attrs"] as $attrname => $attrtype )
			{
				$value = $docinfo["attrs"][$attrname];
				if ( $attrtype==SPH_ATTR_TIMESTAMP )
					$value = date ( "Y-m-d H:i:s", $value );
				print ", $attrname=$value";
			}
			print "\n";
			$n++;
		}
	}
}

//
// $Id$
//

?>