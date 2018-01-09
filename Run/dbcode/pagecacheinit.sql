CREATE TABLE if not exists PageCache (URL VARCHAR PRIMARY KEY  NOT NULL , GENERATION_DATE DATETIME,CONTENT BLOB, NEXT VARCHAR, PAGE_TYPE INTEGER, compressed integer default 0);
alter table PageCache add column regeneration_period integer default 60; 
CREATE INDEX if not exists I_PC_URL ON PageCache (URL ASC);
CREATE INDEX if not exists I_PC_GENERATED ON PageCache (GENERATION_DATE ASC);

-- task table;
CREATE TABLE if not exists PageTasks (
 id integer NOT NULL primary key autoincrement default 0,
 type integer not null, 
 parts integer default 0, 
 entities integer default 0, 
 created DATETIME, 
 scheduled_to DATETIME, 
 started DATETIME, 
 finished DATETIME, 
 results varchar, 
 allowed_retry_count integer default 2,
 allowed_subtask_retry_count integer default 2,
 retries integer default 0, 
 ignore_cache integer default 0,
 refresh_if_needed integer default 0,
 task_comment varchar,
 success integer default 0);
CREATE INDEX if not exists I_PT_ID ON PageTask (id asc);
CREATE INDEX if not exists I_PT_TYPE ON PageTask (type asc);
CREATE INDEX if not exists I_PT_CREATED ON PageTask (created asc);
CREATE INDEX if not exists I_PT_SCHEDULED ON PageTask (scheduled_to asc);
CREATE INDEX if not exists I_PT_SUCCESS ON PageTask (success asc);

-- subtask table;
CREATE TABLE if not exists PageTaskParts (
 task_id integer NOT NULL,
 sub_id integer not null default 0, 
 content varchar, 
 created DATETIME, 
 scheduled_to DATETIME, 
 started DATETIME, 
 finished DATETIME, 
 success integer default 0,
 retries integer default 0, 
 PRIMARY KEY (task_id, sub_id));
CREATE INDEX if not exists I_PTP_PK ON PageTaskParts (task_id asc, sub_id asc);
CREATE INDEX if not exists I_PTP_STARTED ON PageTaskParts (started asc);
CREATE INDEX if not exists I_PTP_ERROR_CODE ON PageTaskParts (success asc);


-- subtask action table;
CREATE TABLE if not exists PageTaskActions (
 action_uuid integer NOT NULL,
 task_id integer NOT NULL,
 sub_id integer,
 started DATETIME, 
 finished DATETIME, 
 success integer default 0,
 PRIMARY KEY (action_uuid));
 
CREATE INDEX if not exists I_PTA_PK ON PageTaskActions (action_uuid asc);
CREATE INDEX if not exists I_PTA_TASK_KEY ON PageTaskActions (task_id asc, sub_id asc);
CREATE INDEX if not exists I_PTA_TASK_ID ON PageTaskActions (task_id asc);
CREATE INDEX if not exists I_PTA_SUB_ID ON PageTaskActions (sub_id asc);
CREATE INDEX if not exists I_PTA_STARTED ON PageTaskActions (started asc);
CREATE INDEX if not exists I_PTA_FINISHED ON PageTaskActions (finished asc);


-- failed pages for subtasks;
CREATE TABLE if not exists PageWarnings (
 id integer NOT NULL primary key autoincrement default 0,
 action_uuid varchar,
 task_id integer NOT NULL,
 sub_id integer NOT NULL,
 URL VARCHAR,
 attempted_at datetime,
 last_seen datetime,
 error_code integer default 0,
 error_level integer default 0,
 error varchar,
 PRIMARY KEY (id)
 );
CREATE INDEX if not exists I_PW_ID ON PageWarnings (id asc);
CREATE INDEX if not exists I_PW_ACTION_ID ON PageWarnings (action_id asc);
CREATE INDEX if not exists I_PW_TASK_ID ON PageWarnings (task_id asc);
CREATE INDEX if not exists I_PW_SUBTASK_ID ON PageWarnings (subtask_id asc);
CREATE INDEX if not exists I_PW_ERROR_CODE ON PageWarnings (error_code asc);
CREATE INDEX if not exists I_PW_URL ON PageWarnings (URL asc);
CREATE INDEX if not exists I_PW_ERROR_LEVEL ON PageWarnings (error_level asc);
