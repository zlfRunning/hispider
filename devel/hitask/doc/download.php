<?php
$url = "http://www.proxycn.com/html_proxy/30fastproxy-1.html";
$pattern = "/onDblClick=\"clip\('([0-9]*?)\.([0-9]*?)\.([0-9]*?)\.([0-9]*?):([0-9]*?)'\);alert/is";
if(($content = file_get_contents($url)))
{
    if(preg_match_all($pattern, $content, $matches))
    {
        array_shift($matches);
        $ip = $matches;
        for($i = 0; $i < count($ip[0]); $i++)
        {
            if($ip[0][$i] < 255 && $ip[1][$i] < 255 && $ip[2][$i] < 255 
                && $ip[3][$i] < 255 && $ip[4][$i] < 65536)
            {
                echo "\"".$ip[0][$i].".".$ip[1][$i].".".$ip[2][$i]
                    .".".$ip[3][$i].":".$ip[4][$i]."\",\n";
            }
        }
    }
}

?>
