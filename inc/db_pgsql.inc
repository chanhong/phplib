<?php
/*
 * Session Management for PHP3
 *
 * Copyright (c) 1998-2000 NetUSE AG
 *                    Boris Erdmann, Kristian Koehntopp
 *
 * $Id: db_pgsql.inc,v 1.6 2000/12/13 17:46:50 negro Exp $
 *
 */ 

class DB_Sql {
  var $Host     = "";
  var $Database = "";
  var $User     = "";
  var $Password = "";

  var $Link_ID  = 0;
  var $Query_ID = 0;
  var $Record   = array();
  var $Row      = 0;

  var $type	= "pgsql";

  var $Seq_Table     = "db_sequence";

  var $Errno    = 0;
  var $Error    = "";
  var $Debug    = 0;

  var $Auto_Free = 0; # Set this to 1 for automatic pg_freeresult on 
                      # last record.

  function ifadd($add, $me) {
          if("" != $add) return " ".$me.$add;
  }
  
  /* public: constructor */
  function DB_Sql($query = "") {
      $this->query($query);
  }

  function connect() {
          if ( 0 == $this->Link_ID ) {
                  $cstr = "dbname=".$this->Database.
                  $this->ifadd($this->Host, "host=").
                  $this->ifadd($this->Port, "port=").
                  $this->ifadd($this->User, "user=").
                  $this->ifadd($this->Password, "password=");
                  $this->Link_ID=pg_pconnect($cstr);
                  if (!$this->Link_ID) {
                          $this->halt("Link-ID == false, pconnect failed");
                  }
          }
  }

  /* Quoting Helper - by Dave Beveridge */
  function quote($str) {
        return "'".$this->escape_string($str)."'";
  }
  function escape_string($str) {
        if (!$this->connect()) return 0;
        if (get_magic_quotes_gpc()) $str = stripslashes($str);
        return pg_escape_string($this->Link_ID,$str);
  }
  function quote_identifier($str) {
        $arr = explode(".",$str);
        return '"'.implode('"."',$arr).'"';
  }
  function qi($str) {
        $arr = explode(".",$str);
        return '"'.implode('"."',$arr).'"';
  }

  function query($Query_String) {
    /* No empty queries, please, since PHP4 chokes on them. */
    if ($Query_String == "")
      /* The empty query string is passed on from the constructor,
       * when calling the class without a query, e.g. in situations
       * like these: '$db = new DB_Sql_Subclass;'
       */
      return 0;

    $this->connect();
    
    if ($this->Debug) 
      printf("<br>Debug: query = %s<br>\n", $Query_String);

    $this->Query_ID = pg_Exec($this->Link_ID, $Query_String);
    $this->Row   = 0;

    $this->Error = pg_ErrorMessage($this->Link_ID);
    $this->Errno = ($this->Error == "")?0:1;
    if (!$this->Query_ID) {
      $this->halt("Invalid SQL: ".$Query_String);
    }

    return $this->Query_ID;
  }
  
  function next_record() {
    $this->Record = @pg_fetch_array($this->Query_ID, $this->Row++);
    
    $this->Error = pg_ErrorMessage($this->Link_ID);
    $this->Errno = ($this->Error == "")?0:1;

    $stat = is_array($this->Record);
    if (!$stat && $this->Auto_Free) {
      pg_freeresult($this->Query_ID);
      $this->Query_ID = 0;
    }
    return $stat;
  }

  function seek($pos) {
    $this->Row = $pos;
  }

  function lock($table, $mode = "write") {
    if ($mode == "write") {
      $result = pg_Exec($this->Link_ID, "lock table $table");
    } else {
      $result = 1;
    }
    return $result;
  }
  
  function unlock() {
    return pg_Exec($this->Link_ID, "commit");
  }


  /* public: sequence numbers */
  function nextid($seq_name) {
    $this->connect();

    if ($this->lock($this->Seq_Table)) {
      /* get sequence number (locked) and increment */
      $q  = sprintf("select nextid from %s where seq_name = '%s'",
                $this->Seq_Table,
                $seq_name);
      $id  = @pg_Exec($this->Link_ID, $q);
      $res = @pg_Fetch_Array($id, 0);
      
      /* No current value, make one */
      if (!is_array($res)) {
        $currentid = 0;
        $q = sprintf("insert into %s values('%s', %s)",
                 $this->Seq_Table,
                 $seq_name,
                 $currentid);
        $id = @pg_Exec($this->Link_ID, $q);
      } else {
        $currentid = $res["nextid"];
      }
      $nextid = $currentid + 1;
      $q = sprintf("update %s set nextid = '%s' where seq_name = '%s'",
               $this->Seq_Table,
               $nextid,
               $seq_name);
      $id = @pg_Exec($this->Link_ID, $q);
      $this->unlock();
    } else {
      $this->halt("cannot lock ".$this->Seq_Table." - has it been created?");
      return 0;
    }
    return $nextid;
  }



  function metadata($table) {
    $count = 0;
    $id    = 0;
    $res   = array();

    $this->connect();
/* orig code - doesn't work when table is empty */
    $id = pg_exec($this->Link_ID, "select * from $table");
    if ($id < 0) {
      $this->Error = pg_ErrorMessage($id);
      $this->Errno = 1;
      $this->halt("Metadata query failed.");
    }
    $count = pg_NumFields($id);
    for ($i=0; $i<$count; $i++) {
      $res[$i]["table"] = $table;
      $res[$i]["name"]  = pg_FieldName  ($id, $i); 
      $res[$i]["type"]  = pg_FieldType  ($id, $i);
      $res[$i]["len"]   = pg_FieldSize  ($id, $i);
      $res[$i]["flags"] = "";
    }
/*  This code is even worse.  
    $meta = pg_meta_data($this->Link_ID,$table);
    foreach ($meta as $name => $data) {
	$res[$count]["table"] = $table;
	$res[$count]["name"] = $data["name"];
	$res[$count]["num"] = $data["num"];  
	$res[$count]["len"] = $data["len"];
	$res[$count]["null"] = $data["not null"] ? "" : "null";
	$count++;
    }
*/


    pg_FreeResult($id);
    return $res;
  }

  function affected_rows() {
    return pg_cmdtuples($this->Query_ID);
  }

  function num_rows() {
    return pg_numrows($this->Query_ID);
  }

  function num_fields() {
    return pg_numfields($this->Query_ID);
  }

  function nf() {
    return $this->num_rows();
  }

  function np() {
    print $this->num_rows();
  }

  function f($Name) {
    return $this->Record[$Name];
  }

  function p($Name) {
    print $this->Record[$Name];
  }
  
  function halt($msg) {
    printf("</td></tr></table><b>Database error:</b> %s<br>\n", $msg);
    printf("<b>PostgreSQL Error</b>: %s (%s)<br>\n",
      $this->Errno,
      $this->Error);
    die("Session halted.");
  }

  function table_names() {
    $this->query("select relname from pg_class where relkind = 'r' 
				and relhaspkey = TRUE
				and not relname like 'pg_%'");
    $i=0;
    while ($this->next_record())
     {
      $return[$i]["table_name"]= $this->f(0);
      $return[$i]["tablespace_name"]=$this->Database;
      $return[$i]["database"]=$this->Database;
      $i++;
     }
    return $return;
  }
  function primary_key($table) {
    $this->query("SELECT               
		    pg_attribute.attname, 
		    format_type(pg_attribute.atttypid, pg_attribute.atttypmod) 
		  FROM pg_index, pg_class, pg_attribute 
		  WHERE 
		    pg_class.oid = '".$table."'::regclass AND
		    indrelid = pg_class.oid AND
		    pg_attribute.attrelid = pg_class.oid AND 
		    pg_attribute.attnum = any(pg_index.indkey)
		    AND indisprimary");
    if ($this->next_record()) {
        return $this->f(0);
    }
  }
}
?>
