var oReloadTimer = null;
var oCounterTimer = null;

function reloadDocument() {
	if(!withquery) history.pushState('', document.title, window.location.pathname);
	window.location.reload();
};

function cdpause() {
	clearTimeout(oReloadTimer);
};

function runReloadCounter() {
	var oReloadContent = document.getElementById("ReloadContent");
	if (oReloadContent) {
		if (counter < 0) {
			counter = counterfull;
		}
		counter--;
	}
};

function initDoc() {
	if (oReloadTimer) window.clearInterval(oReloadTimer);
	oReloadTimer = window.setInterval("reloadDocument();", counterfull * 1000);
	if (oCounterTimer) window.clearInterval(oCounterTimer);
	oCounterTimer = window.setInterval("runReloadCounter();", 1000);
};

function gotosite(Action) {
	window.location.href = Action;
}

/* Function for add new reader in readers.html */
function addreader() {
	cdpause();
	$("#searchTable").fadeOut('slow', function() {
		$("#newreader").fadeIn('slow');
	});
}

/* Function for add new user in userconfig.html */
function adduser() {
	cdpause();
	$("#searchTable").fadeOut('slow', function() {
		$("#newuser").fadeIn('slow');
	});
};

String.prototype.toHHMMSS = function () {
	if (this.length < 1) {
		return ''
	}
	var sec_num = parseInt(this, 10); // don't forget the second param
	var years = Math.floor(sec_num / (86400*365));
	var days = Math.floor(sec_num / 86400);
	var hours = Math.floor(sec_num / 3600);
	var minutes = Math.floor((sec_num - (hours * 3600)) / 60);
	var seconds = sec_num - (hours * 3600) - (minutes * 60);
	hours = hours - (24 * days);
	days = days - (365 * years);
	if (years < 1) {
		years = "";
	} else {
		years = years + "y ";
	}
	if (days < 1) {
		days = "";
	} else {
		days = days + "d ";
	}
	if (hours < 10) {
		hours = "0" + hours;
	}
	if (minutes < 10) {
		minutes = "0" + minutes;
	}
	if (seconds < 10) {
		seconds = "0" + seconds;
	}
	var time = days + hours + ':' + minutes + ':' + seconds;
	return time;
}


function runden(value) {
	var k = (Math.round(value * 100) / 100).toString();
	k += (k.indexOf('.') == -1) ? '.00' : '00';
	return k.substring(0, k.indexOf('.') + 3);
}

/*
 * General: Eventhandler
 */
$(function () {
	// Pollinterval UP
	$("#inc").click(function () {
		if (pollintervall > 98000) return;
		$(":text[name='pintervall']").val(Number($(":text[name='pintervall']").val()) + 1);
		pollintervall = $(":text[name='pintervall']").val() * 1000;
		if (!nostorage) {
			sessionStorage.pollintervall = pollintervall;
		}
	});
	// Pollinterval DOWN
	$("#dec").click(function () {
		if (pollintervall < 2000) return;
		$(":text[name='pintervall']").val(Number($(":text[name='pintervall']").val()) - 1);
		pollintervall = $(":text[name='pintervall']").val() * 1000;
		if (!nostorage) {
			sessionStorage.pollintervall = pollintervall;
		}
	});
	// Hover for showing Chart on Statuspage
	$('table.status').on('mouseover', 'tr > td.statuscol14', function (e) {
		var uid = '#' + $(this).parent().attr('id');
		if ('pcr'.indexOf($(uid).attr('class')) >= 0) {
			if ($(uid).data('ecmhistory')) {
				var head = $(uid + ' > td:nth-child(3)').attr('title').indexOf('(') > -1 ? $(uid + ' > td:nth-child(3)').attr('title').substring(0, $(uid + ' > td:nth-child(3)').attr('title').indexOf('(')-1) : $(uid + ' > td:nth-child(3)').attr('title');
				$('#charthead').text(head + ' History');
				$("#graph").html('');
				var arry = $(uid).data('ecmhistory').split(",");
				$.each(arry, function (index, value) {
					$("#graph").append(generateBar(value));
				});
				$("#chart").show();
			}
			$("#chart").offset({
				left: e.pageX + 20,
				top: e.pageY - 20
			});
		}
	});
	// Mousout for hiding Chart on Statuspage
	$('table.status').on('mouseout', 'tr > td.statuscol14', function () {
		$("#chart").hide();
	});

	$("#regexok").click(function () {

		for (var i = 1; i < 6; i++) {
			var pattern = $('#regex' + i).val();
			if (pattern) {
				var color = $('#color' + i).val();
				var fcolor = $('#fcolor' + i).val();
			} else {
				var color = '';
				var fcolor = '';
			}
			localStorage['regex' + i] = pattern ? pattern : '';
			localStorage['color' + i] = color ? color : '';
			localStorage['fcolor' + i] = fcolor ? fcolor : '';
			localStorage['whitelisted' + i] = $('#whitelisted' + i).prop('checked') ? '1' : '0';
			localStorage['hidden' + i] = $('#hidden' + i).prop('checked') ? '1' : '0';
		}

	});

	$("#regexreset").click(function () {

		if (confirm('Delete all Filters and Colors?')) {
			for (var i = 1; i < 6; i++) {
				$('#regex' + i).val('');
				$('#whitelisted' + i).prop('checked', false);
				$('#hidden' + i).prop('checked', false);
				$('#color' + i).val($('.colorPicker_def_color').css('color'));
				$('#color' + i).change();
				$('#fcolor' + i).val($('.colorPicker_def_fcolor').css('color'));   
    			$('#fcolor' + i).change();
				localStorage['regex' + i] = '';
				localStorage['color' + i] = '';
				localStorage['fcolor' + i] = '';
				localStorage['whitelisted' + i] = '0';
				localStorage['hidden' + i] = '0';
			}
		}

	});

	$(".debugls a, .debugl a").click(function () {
		parameters = parameters + "&debug=" + $(this).attr('sendval');
		return false;
	});

	$("#savelog").on('click', function (event) {
		var txt = '';
		$("#livelogdata li").each(function (i) {
			txt += $(this).text() + '\n';
		});
		// Data URI
		txtData = 'data:application/txt;charset=utf-8,' + encodeURIComponent(txt);
		$(this).attr({
			'href': txtData,
			'target': '_blank'
		});
	});

	$("#showhidesettings").click(function () {
		if ($("#showhidesettings").val() == 'Show Settings') {
			$("#showhidesettings").val('Hide Settings');
			$("#regexdata").fadeIn('slow');
		} else {
			$("#showhidesettings").val('Show Settings');
			$("#regexdata").fadeOut('slow');
		}
	});

	$("#stoplog").click(function () {
		if ($("#stoplog").val() == 'Stop Log') {
			$("#stoplog").val('Start Log');
			stoppoll = 1;
		} else {
			$("#stoplog").val('Stop Log');
			stoppoll = 0;
			waitForMsg();
		}
	});

	$("#onlineidle").click(function () {
		if ($("#onlineidle").text() == 'Login*') {
			$("#onlineidle")
				.text('Online & Idle*')
				.attr('title', 'Login info (click to switch)');
		} else {
			$("#onlineidle")
				.text('Login*')
				.attr('title', 'Online & Idle info (click to switch)');
		}
		if (!nostorage) localStorage.loi = $("#onlineidle").text();
		waitForMsg();
	});

	// switch reader ON/OFF
	$("a.switchreader").click(function (e) {
		e.preventDefault();
		var parameters_old = parameters;
		parameters += '&label=' + $(this).data('reader-name') + '&action=' + $(this).data('next-action');
		var rowid = '#' + $(this).data('md5');
		var img = $(this).children("img");
		waitForMsg();
		if ($(this).data('next-action') == 'enable') {
			$(this).data('next-action', 'disable').attr('title', 'Disable this reader');
			$(rowid).attr('class', 'enabledreader');
			img.attr('src', 'image?i=ICDIS').attr('alt', 'Disable this reader');
		} else {
			$(this).data('next-action', 'enable').attr('title', 'Enable this reader');
			$(rowid).attr('class', 'disabledreader');
			img.attr('src', 'image?i=ICENA').attr('alt', 'Enable this reader');
		}
		parameters = parameters_old;
	});

	// delete reader
	$("a.deletereader").click(function (e) {
		e.preventDefault();
		if (confirm("Delete Reader " + $(this).data('reader-name') + "?")) {
			var parameters_old = parameters;
			parameters += '&label=' + $(this).data('reader-name') + '&action=' + $(this).data('next-action');
			waitForMsg();
			parameters = parameters_old;
			$('#' + $(this).data('md5')).fadeOut('slow');
		}
	});

	// switch user ON/OFF
	$("a.switchuser").click(function (e) {
		e.preventDefault();
		var parameters_old = parameters;
		parameters += '&user=' + $(this).data('user-name') + '&action=' + $(this).data('next-action');
		var rowid = '#' + $(this).data('md5');
		var img = $(this).children("img");
		waitForMsg();
		if ($(this).data('next-action') == 'enable') {
			$(this).data('next-action', 'disable').attr('title', 'Disable this user');
			$(rowid).attr('class', 'offline');
			$(rowid + ' > td.usercol2').text('offline');
			img.attr('src', 'image?i=ICDIS').attr('alt', 'Disable this user');
		} else {
			$(this).data('next-action', 'enable').attr('title', 'Enable this user');
			$(rowid).attr('class', 'disabled');
			$(rowid + ' > td.usercol2').text('offline (disabled)');
			img.attr('src', 'image?i=ICENA').attr('alt', 'Enable this user');
		}
		parameters = parameters_old;
	});

	// reset user stats
	$("a.resetuser").click(function (e) {
		e.preventDefault();
		if (confirm("Reset Stats for " + $(this).data('user-name') + "?")) {
			var parameters_old = parameters;
			parameters += '&user=' + $(this).data('user-name') + '&action=' + $(this).data('next-action');
			waitForMsg();
			parameters = parameters_old;
		}
	});

	// delete user
	$("a.deleteuser").click(function (e) {
		e.preventDefault();
		if (confirm("Delete User " + $(this).data('user-name') + "?")) {
			var parameters_old = parameters;
			parameters += '&user=' + $(this).data('user-name') + '&action=' + $(this).data('next-action');
			waitForMsg();
			parameters = parameters_old;
			$('#' + $(this).data('md5')).fadeOut('slow');
		}
	});

	// search related events
	$("#searchTerm").keyup(function () {
		var value = $("#searchTerm").val().toLowerCase().trim();
		$("#dataTable tr").each(function (index) {
			if (!index) return;
			$(this).find("td").each(function () {
				var id = $(this).text().toLowerCase().trim();
				var not_found = (id.indexOf(value) == -1);
				$(this).closest('tr').toggle(!not_found);
				return not_found;
			});
		});
	});

	$("#searchTerm").click(function () {
		cdpause();
	});

	$("#searchTerm").blur(function () {
		initDoc();
	});

	var table = $('#dataTable').stupidtable();

	table.bind('beforetablesort', function (event, data) {
		lockpoll = 1;
		table.addClass("disabledtable");
	});

	table.bind('aftertablesort', function (event, data) {
		// data.column - the index of the column sorted after a click
		// data.direction - the sorting direction (either asc or desc)
		lockpoll = 0;
		table.removeClass("disabledtable");
	});
});

/*
 * General: Update page footer and failbannotifier
 */
function updateFooter(data) {
	$("#curtime").text(' ' + data.oscam.curdate + ' | ' + data.oscam.curtime + ' ');
	$("#runtime").text(' ' + data.oscam.runtime);
	$("#uptime") .text(' ' + data.oscam.uptime);

	if ($("#fbnotifier > span.span_notifier").length) {
		if (data.oscam.failbannotifier > 0) {
			$("#fbnotifier > span.span_notifier")
				.text(data.oscam.failbannotifier);
		}
		else {
			$("#fbnotifier > span.span_notifier").remove();
		}
	}
	else if (data.oscam.failbannotifier > 0) {
		$("#fbnotifier")
			.append('<SPAN CLASS="span_notifier">'+ data.oscam.failbannotifier + '</SPAN>');
	}
}

/*
 *	identfy an element within string of elements
 */
var poll_excluded;

function is_nopoll(value) {
	return (poll_excluded.indexOf(value) > (-1)) ? true : false;
}

/*
 * Userpage Functions: Update Page
 */
function updateUserpage(data) {

	// update user lines
	$.each(data.oscam.users, function (i, item) {
		var uid = "#" + item.user.usermd5;
		poll_excluded = ($(uid).attr('nopoll') != undefined) ? $(uid).attr('nopoll') : '';

		switch (item.user.classname) {
		case 'online':
			$(uid).attr('class', item.user.classname);

			if (!is_nopoll('usercol1')) {
				if ($(uid + " td.usercol1 > span.span_notifier").length) {
					if(item.user.unotify){
						$(uid + " td.usercol1 > span.span_notifier")
							.text(item.user.unotify);
					}
					else {
						$(uid + " td.usercol1 > span.span_notifier").remove();
					}
				}
				else if(item.user.unotify) {
					$(uid + " td.usercol1")
						.append('<SPAN CLASS="span_notifier">'+ item.user.unotify + '</SPAN>');
				}
			}

			if (!is_nopoll('usercol2')) {
				$(uid + " td.usercol2")
					.attr('title', item.user.stats.expectsleep != 'undefined' ? (item.user.stats.expectsleep > 0 ? 'Sleeping in ' + item.user.stats.expectsleep + ' minutes' : 'Sleeping') : '')
					.data('sort-value', item.user.ip)
					.html("<B>" + item.user.status + "</B><br>" + item.user.ip);
			}

			if (!is_nopoll('usercol3')) {
				$(uid + " td.usercol3").html(item.user.stats.idle + "<br>" + item.user.stats.timeonchannel.toHHMMSS());
			}

			if (!is_nopoll('usercol4')) {
				if (item.user.protoicon.length > 0) {
					if (!$(uid + " td.usercol4 > img").length || $(uid + " td.usercol4 > img").attr('src')!='image?i=IC_' + item.user.protoicon) {
						var protoimage = $('<img class="protoicon" src="image?i=IC_' + item.user.protoicon + '" />');
						protoimage.hide();
						$(uid + " td.usercol4").html(protoimage);
						protoimage.fadeIn('slow');
					}
				} else {
					$(uid + " td.usercol4").text(item.user.protocol);
				}

				$(uid + " td.usercol4")
					.attr('title', item.user.prototitle)
					.data('sort-value', item.user.protosort);
			}

			// channel icon
			if (!is_nopoll('usercol6')) {
				$(uid + " td.usercol6")
					.attr('title', item.user.lastchanneltitle)
					.data('sort-value', item.user.lastchannelsort);

				if (item.user.lca.length > 0) {
					// if we already have a picon within link
					if ($(uid + " > td.usercol6 > img.usericon").length) {
						// we compare the picon name and switch if different
						var image = $(uid + " > td.usercol6 > img.usericon");
						if (image.attr('src') != 'image?i=IC_' + item.user.lca) {
							// set title of link as tooltip
							image.hide();
							image.attr('src', 'image?i=IC_' + item.user.lca);
							image.fadeIn('slow');
							image.attr('alt', item.user.lastchanneltitle);
							image.attr('title', item.user.lastchanneltitle);
						}
					} else {
						// we have no image so we have to create one

						// if we have picon clear text
						$(uid + " > td.usercol6").text('');

						// just to be sure that class of image is set
						if ($(uid + " > td.usercol6 > img").length) {
							$(uid + " > td.usercol6 > img").attr('class', 'usericon');
						}

						newimage = $('<img class="usericon" src="image?i=IC_' + item.user.lca + '">');
						newimage.hide();
						$(uid + " > td.usercol6").append(newimage);
						newimage.fadeIn('slow');
						newimage.attr('alt', item.user.lastchanneltitle);
						newimage.attr('title', item.user.lastchanneltitle);
					}
				} else {
					$(uid + " td.usercol6").html(item.user.lastchannel);
				}
			}

			if (!is_nopoll('usercol7')) {
				$(uid + " td.usercol7")
					.text(item.user.stats.cwlastresptimems);
			}
			//usercol8 ???
			if (!is_nopoll('usercol9')) {
				$(uid + " td.usercol9").text(item.user.stats.cwok);
			}
			if (!is_nopoll('usercol10')) {
				$(uid + " td.usercol10").text(item.user.stats.cwnok);
			}
			if (!is_nopoll('usercol11')) {
				$(uid + " td.usercol11").text(item.user.stats.cwignore);
			}
			if (!is_nopoll('usercol12')) {
				$(uid + " td.usercol12").text(item.user.stats.cwtimeout);
			}
			if (!is_nopoll('usercol13')) {
				$(uid + " td.usercol13").text(item.user.stats.cwccyclechecked + ' / ' + item.user.stats.cwcycleok + ' / ' + item.user.stats.cwcyclenok + ' / ' + item.user.stats.cwcycleign);
			}
			if (!is_nopoll('usercol14')) {
				$(uid + " td.usercol14").text(item.user.stats.cwcache);
			}
			if (!is_nopoll('usercol15')) {
				$(uid + " td.usercol15").text(item.user.stats.cwtun);
			}
			if (!is_nopoll('usercol16')) {
				$(uid + " td.usercol16").text(item.user.stats.cwcache);
			}
			if (!is_nopoll('usercol17')) {
				$(uid + " td.usercol17").text(item.user.stats.emmok);
			}
			if (!is_nopoll('usercol18')) {
				$(uid + " td.usercol18").text(item.user.stats.emmnok);
			}
			if (!is_nopoll('usercol19')) {
				$(uid + " td.usercol19").text(item.user.stats.cwrate + item.user.stats.cwrate2);
			}
			if (!is_nopoll('usercol22')) {
				$(uid + " td.usercol22").text(item.user.stats.cascusercomb);
			}
			if (!is_nopoll('usercol21')) {
				$(uid + " td.usercol21").text(item.user.stats.n_requ_m);
			}
			if (!is_nopoll('usercol20')) {
				$(uid + " td.usercol20")
					.attr('title', item.user.expview)
					.text(item.user.stats.expdate);
			}
			break;

		case 'connected':
			$(uid).attr('class', item.user.classname);

			if (!is_nopoll('usercol1')) {
				if ($(uid + " td.usercol1 > span.span_notifier").length) {
					if(item.user.unotify){
						$(uid + " td.usercol1 > span.span_notifier")
							.text(item.user.unotify);
					}
					else {
						$(uid + " td.usercol1 > span.span_notifier").remove();
					}
				}
				else if(item.user.unotify) {
					$(uid + " td.usercol1")
						.append('<SPAN CLASS="span_notifier">'+ item.user.unotify + '</SPAN>');
				}
			}

			if (!is_nopoll('usercol2')) {
				$(uid + " td.usercol2")
					.attr('title', '')
					.data('sort-value', item.user.ip)
					.html("<B>" + item.user.status + "</B><br>" + item.user.ip);
			}

			if (!is_nopoll('usercol3')) {
				$(uid + " td.usercol3").html(item.user.stats.idle + "<br>" + item.user.stats.timeonchannel.toHHMMSS());
			}

			if (!is_nopoll('usercol4')) {
				if (item.user.protoicon.length > 0) {
					if (!$(uid + " td.usercol4 > img").length || $(uid + " td.usercol4 > img").attr('src')!='image?i=IC_' + item.user.protoicon) {
						var protoimage = $('<img class="protoicon" src="image?i=IC_' + item.user.protoicon + '" />');
						protoimage.hide();
						$(uid + " td.usercol4").html(protoimage);
						protoimage.fadeIn('slow');
					}
				} else {
					$(uid + " td.usercol4").text(item.user.protocol);
				}
				$(uid + " td.usercol4")
					.attr('title', item.user.prototitle)
					.data('sort-value', item.user.protosort);
			}

			if (!is_nopoll('usercol6')) {
				// channel icon
				$(uid + " td.usercol6")
					.attr('title', item.user.lastchanneltitle)
					.data('sort-value', item.user.lastchannelsort);

				if (item.user.lca.length > 0) {
					var image;
					if ($(uid + " td.usercol6").html().length == 0) {
						image = $('<img class="usericon" src="image?i=IC_' + item.user.lca + '" />');
						image.hide();
						$(uid + " td.usercol6").prepend(image);
						image.fadeIn('slow');
					} else {
						image = $(uid + " td.usercol6 img.usericon");
						if (image.attr('src') != ('image?i=IC_' + item.user.lca)) {
							image.fadeOut('fast', function () {
								image.attr('src', 'image?i=IC_' + item.user.lca);
								image.fadeIn('slow');
							});
							image.attr('alt', item.user.lcb);
							image.attr('title', item.user.lastchanneltitle);
						}
					}
				} else {
					$(uid + " td.usercol6").html(item.user.lastchannel);
				}
			}

			if (!is_nopoll('usercol7')) {
				$(uid + " td.usercol7")
					.text(item.user.stats.cwlastresptimems);
			}
			if (!is_nopoll('usercol19')) {
				$(uid + " td.usercol19").text(item.user.stats.cwrate);
			}
			break;

		default:
			//check the last status
			if ('online,connected'.indexOf($(uid).attr('class')) > (-1)) {
				// last status was online so cleanup offline
				$(uid).attr('class', item.user.classname);
				if (!is_nopoll('usercol1')) {
					if ($(uid + " td.usercol1 > span.span_notifier").length) {
						$(uid + " td.usercol1 > span.span_notifier").remove();
					}
				}
				if (!is_nopoll('usercol2')) {
					$(uid + " td.usercol2")
						.attr('title', '')
						.html(item.user.status);
				}
				if (!is_nopoll('usercol3')) {
					$(uid + " td.usercol3").text('');
				}
				if (!is_nopoll('usercol4')) {
					$(uid + " td.usercol4")
						.text('')
						.attr('title', '');
					var protoimage = $(uid + " td.usercol4 img.protoicon");
					if (image) {
						protoimage.fadeOut('slow');
						protoimage.remove();
					}
				}

				//channelicon
				if (!is_nopoll('usercol6')) {
					$(uid + " td.usercol6")
						.text('')
						.data('sort-value', '');

					var image = $(uid + " td.usercol6 img.usericon");
					if (image) {
						image.fadeOut('slow');
						image.remove();
					}
				}
				if (!is_nopoll('usercol7')) {
					$(uid + " td.usercol7")
						.text('');
				}
			}
			break;
		}

		if (typeof custompoll == 'function') {
			custompoll(item);
		}

	});

	// update user totals + ECM
	updateTotals(data);

	// update footer
	updateFooter(data);
}

/*
 * Readerpage Functions: Update Page
 */
function updateReaderpage(data) {

	// update reader lines
	$.each(data.oscam.readers, function (i, item) {
		var uid = "#" + item.labelmd5;
		poll_excluded = ($(uid).attr('nopoll') != undefined) ? $(uid).attr('nopoll') : '';

		$(uid).attr('class', item.classname);

		if (!is_nopoll('readercol4')) {
			$(uid + " td.readercol4").text(item.stats.ecmsok + item.stats.ecmsokrel)
				.data('sort-value', item.stats.ecmsok);
		}
		if (!is_nopoll('readercol5')) {
			$(uid + " td.readercol5").text(item.stats.ecmsnok + item.stats.ecmsnokrel)
				.data('sort-value', item.stats.ecmsnok);
		}
		if (!is_nopoll('readercol6')) {
			$(uid + " td.readercol6").text(item.stats.ecmsfiltered);
		}
		if (!is_nopoll('readercol7')) {
			$(uid + " td.readercol7").text(item.stats.emmerror);
		}
		if (!is_nopoll('readercol8')) {
			$(uid + " td.readercol8").text(item.stats.emmwritten);
		}
		if (!is_nopoll('readercol9')) {
			$(uid + " td.readercol9").text(item.stats.emmskipped);
		}
		if (!is_nopoll('readercol10')) {
			$(uid + " td.readercol10").text(item.stats.emmblocked);
		}
		if (!is_nopoll('readercol11')) {
			$(uid + " td.readercol11").text(item.stats.lbweight);
		}

		if (typeof custompoll == 'function') {
			custompoll(item);
		}
	});

	// update user totals + ECM
	updateTotals(data);

	// update footer
	updateFooter(data);

}

/*
 *	LiveLog Functions: format the debuglevel switcher
 */
function setDebuglevel(debug, maxdebug) {
	var cs_dblevel = parseInt(debug);
	var maxlevel = parseInt(maxdebug);
	if (lastdebuglevel != cs_dblevel) {
		var lvl = 0;
		$("#debugfrom").text(' Switch Debug from ' + cs_dblevel + ' to ');
		for (var i = 0; i < maxlevel; i++) {
			lvl = 1 << i;
			if (cs_dblevel & lvl) {
				$("#debug" + lvl).attr('sendval', cs_dblevel - lvl);
			} else {
				$("#debug" + lvl).attr('sendval', cs_dblevel + lvl);
			}
			$("#debug" + lvl).attr('class', (cs_dblevel & lvl) ? 'debugls' : 'debugl');
		}
		lastdebuglevel = cs_dblevel;
	}
}

/*
 * Livelog Functions: get filter color
 */
function getLogColor(text) {

	if (nostorage) {
		return null;
	}

	for (var i = 1; i < 6; i++) {
		var pattern = localStorage['regex' + i];
		var color = localStorage['color' + i];
		var fcolor = localStorage['fcolor' + i];
		var hidden = localStorage['hidden' + i];
		var regex = new RegExp(pattern);
		if (pattern && (pattern != '') && (regex.exec(text))) {
			return {
				color: color,
				fcolor: fcolor,
				hidden: hidden
			}
		}
	}
	return null;
}

/*
 * Livelog Functions: get whitelist state
 */
function isWhitelisted(text) {

	if (nostorage) {
		return 1;
	}

	var numwhite = 0;
	for (var i = 1; i < 6; i++) {
		numwhite += parseInt(localStorage['whitelisted' + i]);
	}
	if (numwhite > 0) {
		for (var i = 1; i < 6; i++) {
			var whitelisted = localStorage['whitelisted' + i];
			var pattern = localStorage['regex' + i];
			var regex = new RegExp(pattern);
			if (pattern && (pattern != '') && (whitelisted == '1') && (regex.exec(text))) {
				return 1;
			}
		}
		return 0;
	} else {
		return 1;
	}
}

/*
 * LiveLog Functions: manage the delivered data / loglines
 */
function updateLogpage(data) {

	lockpoll = 1;

	if (data.oscam.debug) {
		setDebuglevel(data.oscam.debug, data.oscam.maxdebug);
	}
	if (data.oscam.logdisabled) {
		stoppoll = 1;
		$("#livelogdata").append('<li>Log is disabled</li>\n');
	}

	$.each(data.oscam.lines, function (i, item) {

		if (isWhitelisted(item.line)) {
			var newcolor = getLogColor(item.line);
			var newline = $('<li class="' + item.usr + '">' + item.line + '</li>\n');
			var hiddenline = 0;
			if (newcolor) {
				if (newcolor.hidden != '1') {
					if (newcolor.color && newcolor.color != '') {
						newline.css('background-color', newcolor.color);
					}
					if (newcolor.fcolor && newcolor.fcolor != '') {
						newline.css('color', newcolor.fcolor);
					}
					$("#livelogdata").append(newline);
				} else {
					hiddenline = 1;
				}
			} else {
				$("#livelogdata").append(newline);
			}

			if (!hiddenline) {
				if ($("#livelogdata li").length >= maxloglines) {
					$("#livelogdata li").eq(0).remove();
				}
				if ($("#livelog:hover").length) {
					$('#livelog').stop(true);
				} else {
					$("#livelog").scrollTop($("#livelog").prop("scrollHeight"));
				}
			}
		}
		parameters = "?lastid=" + item.id;
	});

	// update footer
	updateFooter(data);

	lockpoll = 0;

}

/*
 * Statuspage Functions: JQuery Extensions
 */
$.fn.toHtmlString = function () {
	return $('<td></td>').html($(this).clone()).html();
};

/*
 *	Statuspage Functions: Generate a Bar for Barchart
 */
function generateBar(value) {
	var bar = $('<div class="bar"/>');
	var maxheight = 75; //$( "#graph" ).height() -15;
	var numval = parseInt(value);
	numval = Math.floor(numval / 30);
	if (numval >= maxheight) {
		bar.css('background-color', '#FF0000');
		numval = maxheight;
	}
	bar.css('height', numval + 'px');
	return bar;
}

/*
 *	Statuspage Functions: Add/Remove Subheadline
 */
function addremoveSubheadline(remove, data, container, subheadline, type) {

	if (remove == 1 && $("#" + subheadline).length) {
		$("#" + subheadline)
			.fadeOut('slow')
			.remove();
		$(".status tbody:empty").hide();
	}

	if (remove == 0 && !$("#" + subheadline).length) {
		$(container).removeAttr('style');
		var strheadline = '<TR id="' + subheadline + '"><TD CLASS="subheadline" COLSPAN="12">';
		if (type == 'c') {
			if (data.oscam.status.ucac != '') { //hide idle clients
				strheadline += '<P id="chead">Clients <span id="ucs">' + data.oscam.status.ucs + '</span>/<span id="uca">' + data.oscam.status.uca + '</span> (<span id="ucac">' + data.oscam.status.ucac + '</span> with ECM within last <span id="cfgh">' + data.oscam.status.cfgh + '</span> seconds)</P>'
			} else {
				strheadline += '<P id="chead">Clients <span id="ucs">' + data.oscam.status.ucs + '</span>/<span id="uca">' + data.oscam.status.uca + '</span></P>'
			}
			strheadline += '<DIV><input type="button" onclick="window.location.href = \'status.html?hideidle=5\';" value="Show Hidden" title="Show Hidden User">';
			strheadline += '<input type="button" onclick="window.location.href = \'status.html?hideidle=0\';" value="Show Idle" title="Show Idle User">';
			strheadline += '<input type="button" onclick="window.location.href = \'status.html?hideidle=1\';" value="Hide Idle" title="Hide Idle User">';
		} else if (type == 'm') {
			strheadline += '<P id="shead">Server <span id="scs">' + data.oscam.status.scs + '</span>/<span id="sca">' + data.oscam.status.sca + '</span> & Monitors <span id="mcs">' + data.oscam.status.mcs + '</span>/<span id="mca">' + data.oscam.status.mca + '</span></P>'
			strheadline += '<DIV><input type="button" onclick="window.location.href = \'status.html?hideidle=2\';" value="Show Hidden" title="Show Hidden Server & Monitors">';
		}
		strheadline += '</DIV></TD></TR>';
		var headline = $(strheadline);
		headline.hide();
		$(container).append(headline);
		headline.fadeIn('slow');
	}
}

/*
 *	Statuspage Functions: Update Totals cacheEx
 */
function updateCacheextotals(data) {
	$("#total_cachexpush").text(data.oscam.totals.total_cachexpush);
	$("#total_cachexgot").text(data.oscam.totals.total_cachexgot);
	$("#total_cachexhit").text(data.oscam.totals.total_cachexhit);
	$("#rel_cachexhit").text(data.oscam.totals.rel_cachexhit);
	$("#total_cachesize").text(data.oscam.totals.total_cachesize);
}

/*
 *	Statuspage Functions: Update Totals User + ECM
 */
function updateTotals(data) {
	$("#total_users").text(data.oscam.totals.total_users);
	$("#total_active").text(data.oscam.totals.total_active);
	$("#total_connected").text(data.oscam.totals.total_connected);
	$("#total_online").text(data.oscam.totals.total_online);
	$("#total_disabled").text(data.oscam.totals.total_disabled);
	$("#total_expired").text(data.oscam.totals.total_expired);
	$("#total_cwok").text(data.oscam.totals.total_cwok);
	$("#rel_cwok").text(data.oscam.totals.rel_cwok);
	$("#total_cwcache").text(data.oscam.totals.total_cwcache);
	$("#rel_cwcache").text(data.oscam.totals.rel_cwcache);
	$("#total_cwnok").text(data.oscam.totals.total_cwnok);
	$("#rel_cwnok").text(data.oscam.totals.rel_cwnok);
	$("#total_cwtout").text(data.oscam.totals.total_cwtout);
	$("#rel_cwtout").text(data.oscam.totals.rel_cwtout);
	$("#total_cwign").text(data.oscam.totals.total_cwign);
	//$( "#rel_cwign" ).text( data.oscam.totals.rel_cwign );
	$("#total_ecm_min").text(data.oscam.totals.total_ecm_min);
	$("#total_cw").text(data.oscam.totals.total_cw);
	$("#total_cwpos").text(data.oscam.totals.total_cwpos);
	$("#rel_cwpos").text(data.oscam.totals.rel_cwpos);
	$("#total_cwneg").text(data.oscam.totals.total_cwneg);
	$("#rel_cwneg").text(data.oscam.totals.rel_cwneg);
	$("#total_emok").text(data.oscam.totals.total_emok);
	$("#rel_emok").text(data.oscam.totals.rel_emok);
	$("#total_emnok").text(data.oscam.totals.total_emnok);
	$("#rel_emnok").text(data.oscam.totals.rel_emnok);
	$("#total_em").text(data.oscam.totals.total_em);
}

/*
 *	Statuspage Functions: Update Totals Sysinfo
 */
var first_run = 1;

function updateSysinfo(data) {
	$("#mem_cur_total").text(data.oscam.sysinfo.mem_cur_total);
	$("#mem_cur_free").text(data.oscam.sysinfo.mem_cur_free);
	$("#mem_cur_used").text(data.oscam.sysinfo.mem_cur_used);
	$("#mem_cur_buff").text(data.oscam.sysinfo.mem_cur_buff);
	$("#mem_cur_cached").text(data.oscam.sysinfo.mem_cur_cached);
	$("#mem_cur_freem").attr('title', 'max Free: ' + data.oscam.sysinfo.mem_cur_freem + ' \n(incl. Buffer & Cached)');
	$("#mem_cur_totalsw").text(data.oscam.sysinfo.mem_cur_totalsw);
	$("#mem_cur_freesw").text(data.oscam.sysinfo.mem_cur_freesw);
	$("#mem_cur_usedsw").text(data.oscam.sysinfo.mem_cur_usedsw);
	$("#mem_cur_shared").text(data.oscam.sysinfo.mem_cur_shared);
	$("#oscam_vmsize").text(data.oscam.sysinfo.oscam_vmsize);
	$("#oscam_rsssize").text(data.oscam.sysinfo.oscam_rsssize);
	$("#server_procs").text(data.oscam.sysinfo.server_procs);
	$("#cpu_load_0").text(data.oscam.sysinfo.cpu_load_0);
	$("#cpu_load_1").text(data.oscam.sysinfo.cpu_load_1);
	$("#cpu_load_2").text(data.oscam.sysinfo.cpu_load_2);
	if (!first_run) {
		$("#oscam_refresh").text(data.oscam.sysinfo.oscam_refresh);
		$("#oscam_cpu_user").text(data.oscam.sysinfo.oscam_cpu_user);
		$("#oscam_cpu_sys").text(data.oscam.sysinfo.oscam_cpu_sys);
		$("#oscam_cpu_sum").text(data.oscam.sysinfo.oscam_cpu_sum);
	}
	first_run = 0;
}

/*
 *	Statuspage Functions: Update Page
 */
function updateStatuspage(data) {

	var updatedclients = "";
	var cardokreader = 0;
	var connectedproxys = 0;
	// update status lines
	$.each(data.oscam.status.client, function (i, item) {
		if (item.connection.status == 'CARDOK') cardokreader++;
		if (item.connection.status == 'CONNECTED') connectedproxys++;
		var newrow;

		//add ID's for type c and m to list of existing elements. We need this to delete all not longer existing
		updatedclients += item.thid + ",";

		var uid = "#" + item.thid;
		poll_excluded = ($(uid).attr('nopoll') != undefined) ? $(uid).attr('nopoll') : '';

		if (!$(uid).length && 'rpcxm'.indexOf(item.type) > (-1)) {
			//build new row
			var rowcontent = '<TR ID="' + item.thid + '"><TD CLASS="statuscol0"/><TD CLASS="statuscol1"/><TD CLASS="statuscol4"/>';
			rowcontent += '<TD CLASS="statuscol5"/><TD CLASS="statuscol7"/><TD CLASS="statuscol8"/><TD CLASS="statuscol9"/>';
			rowcontent += '<TD CLASS="statuscol12"/><TD CLASS="statuscol13"/><TD CLASS="statuscol14"/><TD CLASS="statuscol15"/>';
			rowcontent += '<TD CLASS="statuscol16"/></TR>';
			newrow = $(rowcontent);
			newrow.hide();
			// if we have no clients we have to add the headline first

			// append new clientrow to table
			var container = '';
			if ('hms'.indexOf(item.type) > (-1)) {
				container = '#tbodys';
				if (item.type == 'm') {
					if (!$("#Serverheadline").length) {
						addremoveSubheadline(0, data, container, "Serverheadline", item.type);
					} else if (!$("#mca").length) {
						$("#shead").append(' & Monitors <span id="mcs">' + data.oscam.status.mcs + '</span>/<span id="mca">' + data.oscam.status.mca + '</span>');
					}
				}
			} else if ('px'.indexOf(item.type) > (-1)) {
				container = '#tbodyp';
			} else {
				container = '#tbody' + item.type;
				if (!$("#Userheadline").length && item.type == 'c') {
					addremoveSubheadline(0, data, container, "Userheadline", item.type);
				}
			}
			$(container).append(newrow);

			var name1, name2, kill1, kill2, kill3, edit1;
			switch (item.type) {
			case 'c':
			case 'm':
				name1 = 'User';
				name2 = item.name_enc;
				kill1 = '" href="status.html?action=kill&threadid=' + item.thid.substring(3, item.thid.length);
				kill2 = 'Kill the '
				kill3 = 'ICKIL';
				edit1 = 'user_edit.html?user=';
				break;
			case 'r':
			case 'p':
			case 'x':
				name1 = (item.type == 'r') ? 'Reader' : 'Proxy';
				name2 = item.rname_enc;
				kill1 = '" href="status.html?action=restart&label=' + item.name;
				kill2 = 'Restart the ';
				kill3 = 'ICRES';
				edit1 = 'readerconfig.html?label=';
				break;
			}

			if (!is_nopoll('statuscol0')) {
				$(uid + " > td.statuscol0").append('<a title="Hide the ' +
					name1 + ' ' + item.name + '" href="status.html?hide=' +
					item.thid.substring(3, item.thid.length) +
					'"><img class="icon" alt="Hide the' +
					name1 + ' ' + item.name + '" src="image?i=ICHID"></img>');
			}

			if (!is_nopoll('statuscol1')) {
				$(uid + " > td.statuscol1").append('<a title="' + kill2 +
					name1 + ' ' + item.name + kill1 + '"><img class="icon" alt="' +
					kill2 + name1 + ' ' + item.name + '" src="image?i=' + kill3 + '"></img>');
			}

			if (!is_nopoll('statuscol4')) {
				if (data.oscam.piconenabled == "1" && !item.upicmissing) {
					$(uid + " > td.statuscol4").append('<a href="' + edit1 + name2 + '"><img class="statususericon" title="Edit ' +
						name1 + ': ' + item.name + '\n' + item.desc + '" src="image?i=IC_' + name2 + '"></img></a>');
				} else {
					$(uid + " > td.statuscol4").append('<a href="' + edit1 + name2 + '" title="Edit ' + name1 + ': ' +
						item.name + '\n' + item.desc + '\n' + item.upicmissing + '">' + item.name + '</a>');
				}
			}

			if (!is_nopoll('statuscol13')) {
				$(uid + " > td.statuscol13").append('<A HREF="files.html?file=oscam.srvid" TITLE="' + item.request + '"/>');
			}

			if (!is_nopoll('statuscol9')) {
				if (data.oscam.piconenabled == "1" && item.protoicon) {
					$(uid + " > td.statuscol9").append('<img class="protoicon" title="Protocol ' + item.protocol + ' ' + 
						item.protocolext + '" alt="IC_' + item.protoicon + '" src="image?i=IC_' + item.protoicon + '"></img>');
				} else {
					$(uid + " > td.statuscol9").attr('title', item.protocolext).text(item.protocol);
				}
			}
		}

		$(uid).attr('class', item.type).data('ecmhistory', item.request.ecmhistory).removeAttr('style');

		// fix for anonymous newcamd-clients
		if ($(uid + " > td.statuscol4").text().match('anonymous')) {
			if (!is_nopoll('statuscol9')) {
				if (data.oscam.piconenabled == "1" && item.protoicon) {
					$(uid + " > td.statuscol9").html('<img class="protoicon" title="Protocol ' + item.protocol + ' ' + 
						item.protocolext + '" alt="IC_' + item.protoicon + '" src="image?i=IC_' + item.protoicon + '"></img>');
				} else {
					$(uid + " > td.statuscol9").attr('title', item.protocolext).text(item.protocol);
				}
			}

			if (!is_nopoll('statuscol4')) {
				if (data.oscam.piconenabled == "1" && !item.upicmissing) {
					$(uid + " > td.statuscol4").html('<a href="user_edit.html?user=' + item.name_enc +
						'"><img class="statususericon" title="Edit User: ' + item.name + item.desc +
						'" src="image?i=IC_' + item.name_enc + '"></img></a>');
				} else {
					$(uid + " > td.statuscol4").html('<a href="user_edit.html?user=' + item.name_enc + '" title="Edit User: ' +
						item.name + item.desc + item.upicmissing + '">' + item.name + '</a>');
				}
			}
		}

		if (!is_nopoll('statuscol5')) {
			switch (item.au) {
			case '0':
				$(uid + " > td.statuscol5").text('OFF').attr('class', 'statuscol5 statuscol5OFF');
				break;
			case '-1':
				$(uid + " > td.statuscol5").html('<a class="tooltip" href="#">ON<span>' + item.aufmt + '</span></a>').attr('class', 'statuscol5 statuscol5ON');
				break;
			default:
				$(uid + " > td.statuscol5").html('<a class="tooltip" href="#">ACTIVE<span>' + item.aufmt + '</span></a>').attr('class', 'statuscol5 statuscol5ACTIVE');
				break;
			}
		}

		if (!is_nopoll('statuscol4')) {
			$(uid + " > td.statuscol4").attr('title', item.name + (item.desc ? '\n' + item.desc.replace('&#013;', '') : ''));
		}
		if (!is_nopoll('statuscol7')) {
			$(uid + " > td.statuscol7").text(item.connection.ip);
		}
		if (!is_nopoll('statuscol8')) {
			$(uid + " > td.statuscol8").text(item.connection.port);
		}
		if (!is_nopoll('statuscol9')) {
			$(uid + " > td.statuscol9").attr('title', item.protocolext);
		}
		if (!is_nopoll('statuscol12')) {
			$(uid + " > td.statuscol12").text(item.request.caid + ':' + item.request.srvid);
		}

		if (!is_nopoll('statuscol13')) {
			var newimage;

			if (data.oscam.piconenabled == '1' && item.request.srvid != '0000' && item.request.picon) {

				// if we already have a picon within link
				if ($(uid + " > td.statuscol13 > a > img.statususericon").length) {
					// we compare the picon name and switch if different
					var image = $(uid + " > td.statuscol13 > a > img.statususericon");
					if (image.attr('src') != 'image?i=IC_' + item.request.picon) {
						// set title of link as tooltip
						$(uid + " > td.statuscol13 > a").attr('title', item.request.chprovider + item.request.chname);
						image.hide();
						image.attr('src', 'image?i=IC_' + item.request.picon);
						image.fadeIn('slow');
					}
				} else {
					// we have no image so we have to create one

					// if we have picon clear text
					$(uid + " > td.statuscol13").text('');

					// if we have no link we create one
					if (!$(uid + " > td.statuscol13 > a").length) {
						$(uid + " > td.statuscol13").append('<a href="files.html?file=oscam.srvid"/>');
					}
					// set title of link as tooltip
					$(uid + " > td.statuscol13 > a").attr('title', item.request.chprovider + item.request.chname);

					// just to be sure that class of image is set
					if ($(uid + " > td.statuscol13 > a > img").length) {
						$(uid + " > td.statuscol13 > a > img").attr('class', 'statususericon');
					}

					newimage = $('<img class="statususericon" src="image?i=IC_' + item.request.picon + '">');
					newimage.hide();
					$(uid + " > td.statuscol13 > a").append(newimage);
					newimage.fadeIn('slow');
				}

			} else {
				// picon is not delivered in JSON - we set the text of column
				if (item.request.chprovider && item.request.chname && item.request.srvid != '0000') {
					$(uid + " > td.statuscol13").html(item.request.chprovider + item.request.chname);
				} else {
					$(uid + " > td.statuscol13").html('');
				}
			}
		}

		if (!is_nopoll('statuscol14')) {
			if (item.type == 's' || item.type == 'h' || item.type == 'm') {
				$(uid + " > td.statuscol14").text('');
			} else {
				var value = item.type == 'c' ? (item.request.answered ? item.request.answered + ' (' + item.request.msvalue + ' ms)' : '') : item.request.lbvalue;
				if (data.oscam.lbdefined) {
					var label = item.rname_enc.replace('+%28cache%29', '');
					var name = item.type == 'c' ? item.request.answered.replace(' (cache)', '') : item.name;
					if (!$(uid + " > td.statuscol14 > a").length) {
						$(uid + " > td.statuscol14")
							.text('')
							.append('<a href="readerstats.html?label=' + label + '&amp;show=0" TITLE="Show statistics for: ' + name + '">');
					} else {
						$(uid + " > td.statuscol14 > a")
							.attr('href','readerstats.html?label=' + label + '&show=0')
							.attr('title','Show statistics for: ' + name);
					}
					$(uid + " > td.statuscol14 > a").text(value);
				} else {
					$(uid + " > td.statuscol14").text(value);
				}
			}
		}

		if (!is_nopoll('statuscol15')) {
			if ($("#onlineidle").text() != 'Login*') {
				$(uid + " > td.statuscol15")
					.html(item.times.online.toHHMMSS() + '<br>' + item.times.idle.toHHMMSS())
					.attr('title', 'Login: ' + item.times.loginfmt);
			} else {
				$(uid + " > td.statuscol15")
					.html(item.times.loginfmt.substring(0, 8) + '<br>' + item.times.loginfmt.substring(10, 18))
					.attr('title', 'Online: ' + item.times.online.toHHMMSS() + '\nIDLE: ' + item.times.idle.toHHMMSS());
			}
		}

		if (!is_nopoll('statuscol16')) {
			var entitlement = '';

			switch (item.type) {
			case 'r':
				// entitlement for native cards

				var activeentitlements = item.connection.entitlements.length;
				if (activeentitlements > 0) {
					entitlement += '<br><a href="entitlements.html?label=' + item.rname_enc + '&hideexpired=1" class="tooltip">';
					entitlement += '(' + activeentitlements + ' entitlement' + ((activeentitlements != 1) ? 's)' : ')');
					entitlement += '<span>';
					$.each(item.connection.entitlements, function (i, obj) {
						entitlement += obj.caid + ':' + obj.provid + '<br>' + obj.exp + '<br><br>';
					});
					entitlement = entitlement.substring(0, entitlement.length - 4);
					entitlement += '</span></a>';
				} else {
					entitlement += '<br><a href="entitlements.html?label=';
					entitlement += item.rname_enc + '&hideexpired=1" class="tooltip1">(no entitlements)<span>No active entitlements found</span></a>';
				}
				break;

			case 'p':
				if (item.connection.entitlements.length > 0 && item.protocol.indexOf('cccam') > -1) {
					// cccam
					var entobj = item.connection.entitlements[0];
					entitlement += '<br><a href="entitlements.html?label=' + item.rname_enc + '" class="tooltip' + entobj.cccreshare + '">';
					entitlement += '(' + entobj.locals + ' of ' + entobj.cccount + ' cards)';
					entitlement += '<span>card_count=' + entobj.cccount + '<br>';
					entitlement += 'hop1=' + entobj.ccchop1 + '<br>';
					entitlement += 'hop2=' + entobj.ccchop2 + '<br>';
					entitlement += 'hopx=' + entobj.ccchopx + '<br>';
					entitlement += 'currenthops=' + entobj.ccccurr + '<br><br>';
					entitlement += 'reshare0=' + entobj.cccres0 + '<br>';
					entitlement += 'reshare1=' + entobj.cccres1 + '<br>';
					entitlement += 'reshare2=' + entobj.cccres2 + '<br>';
					entitlement += 'resharex=' + entobj.cccresx + '</span></a>';
				}
				if (item.protocol.indexOf('gbox') > -1) {
					// TO DO gbox
					var $html = $(uid + " > td.statuscol16").toHtmlString();
					if ($html != undefined) {
						entitlement = $html.substring($html.indexOf('<br>'), $html.indexOf('</a>'));
						if (entitlement) entitlement += '</a>';
					}
				}
				break;
			}

			$(uid + " > td.statuscol16").empty().html(item.connection.status + entitlement).attr('class', 'statuscol16 statuscol16' + item.connection.status);
		}

		if (newrow) {
			newrow.fadeIn("slow");
		}

		if (typeof custompoll == 'function') {
			custompoll(item);
		}

	});

	//remove non existing elements
	$("tr.c, tr.m, tr.r, tr.p, tr.h").each(function () {
		if (updatedclients.indexOf($(this).attr('id')) == -1) {
			$(this).fadeOut('slow').remove();
		}
	});

	// if we have no clients left we remove the headline
	if (!$("tr.c").length && data.oscam.status.uca == '0') {
		addremoveSubheadline(1, '', '', "Userheadline", 'c');
	}
	// if we have no servers/monitors left we remove the headline
	if (!$("tr.m").length && data.oscam.status.mca == '0') {
		if ($("#mca").length) {
			$("#shead").replaceWith('<P id="shead">Server <span id="scs">' + data.oscam.status.scs + '</span>/<span id="sca">' + data.oscam.status.sca + '</span></P>');
		}
		if (!$("tr.s").length && !$("tr.h").length && data.oscam.status.sch == '0') {
			addremoveSubheadline(1, '', '', "Serverheadline", 'm');
		}
	}

	//update client-headline
	if (data.oscam.status.uca != '0') {
		if (!$("#Userheadline").length) {
			addremoveSubheadline(0, data, "#tbodyc", "Userheadline", "c");
		} else {
			$("#ucs").text(data.oscam.status.ucs);
			$("#uca").text(data.oscam.status.uca);
			if (data.oscam.status.ucac != '0') $("#ucac").text(data.oscam.status.ucac);
		}
	}
	//update server/monitor-headline
	if (data.oscam.status.mca != '0' && $("#mcs")) {
		$("#mcs").text(data.oscam.status.mcs);
		$("#mca").text(data.oscam.status.mca);
	}

	//update reader-headline
	$("#rcc").text(cardokreader);

	//update proxy-headline
	$("#pcc").text(connectedproxys);

	// update footer
	updateFooter(data);

	// sysinfos
	if ($("#mem_cur_total").length) updateSysinfo(data);

	// user + ecm totals
	if ($("#total_users").length) updateTotals(data);

	// cachex
	if ($("#total_cachexpush").length) updateCacheextotals(data);

}


/*
 * Cacheexpage Functions: Update Page
 */
function updateCacheexpage(data) {

	updateCacheextotals(data);
	
}

/*
 *	General fork into page refresh functions
 */
function updatePage(data) {

	// show heartbeat
	if ($("input.pintervall").length && $("input.pintervall").css("background-color") != $("#picolor").css("background-color")) {
		var orgstyle = $("input.pintervall").css("background-color");
		$("input.pintervall").css("background-color", $("#picolor").css("background-color"));
	}

	switch (page) {
	case 'status':
		updateStatuspage(data);
		break;
	case 'user':
		updateUserpage(data);
		break;
	case 'reader':
		updateReaderpage(data);
		break;
	case 'livelog':
		updateLogpage(data);
		break;
	case 'cacheex':
		updateCacheexpage(data);
		break;
	default:
		break;
	}

	// hide heartbeat
	if ($("input.pintervall").length && $("input.pintervall").css("background-color") == $("#picolor").css("background-color")) {
		setTimeout(function () {
			$("input.pintervall").css("background-color", orgstyle);
		}, 300);
	}

	if (typeof afterpoll == 'function') {
		afterpoll();
	}
}

function setPollerr(error) {
	if (error && !$("#pollerr").length) {
		$("body").append('<div id="pollerr" style="top:5px;left:5px;background-color:red;color:yellow;">POLLERR</div>');
	} else {
		if ($("#pollerr").length) {
			$("#pollerr").fadeOut('slow').remove();
		}
	}
}

/*
 * General Polling
 */
var lockpoll = 0;
var timer_ID;

function waitForMsg() {

	if (typeof pollrefresh == 'undefined') return;

	if (lockpoll > 0) {
		/* assumed that previous poll is not finnished yet we not
	   call new data and just set the next intervall */
		clearTimeout(timer_ID);
		timer_ID = setTimeout("waitForMsg()", pollintervall);
		return;
	}

	$.ajax({
		type: "GET",
		url: jsonurl + parameters,
		dataType: "JSON",
		async: true,
		cache: false,
		success: function (data) {
			setPollerr(0);
			updatePage(data);
			if (!pollrefresh && page != 'livelog') return;
			if (!stoppoll) {
				clearTimeout(timer_ID);
				timer_ID = setTimeout("waitForMsg()", pollintervall);
			}
		},
		error: function (XMLHttpRequest, textStatus, errorThrown) {
			clearTimeout(timer_ID);
			timer_ID = setTimeout("waitForMsg()", 15000);
			setPollerr(1);
		}
	});
}

/*
 * General: Set Poll Interval
 */
function setPollrefresh() {
	// Set pollintervall, if pollrefresh set to 0 disable polling
	if (pollrefresh) {
		pollintervall = parseInt(pollrefresh) * 1000;
		if (pollintervall > 99000) pollintervall == 99000;
		if (!nostorage) {
			if (sessionStorage.pollintervall) pollintervall = sessionStorage.pollintervall;
			else sessionStorage.pollintervall = pollintervall;
		}
	}
}

// static for paranoid Browsers
var nostorage = 0;

/*
 * General: Start Polling
 */
$(document).ready(function () {

	if (!localStorage) {
		nostorage = 1;
		// remove whole filter block - makes no sense
		// without saving
		$('#regex').remove();
	}

	// set default to nothing excluded
	poll_excluded = '';

	// help wiki links
	if (typeof oscamconf != "undefined") {
		var language = $('meta[http-equiv="language"]').attr("content");
		var wikihref = "http://www.streamboard.tv/wiki/OSCam/" + language + "/Config/oscam." + oscamconf + "#";
		$("form table a").click(function () {
			if (!$(this).attr("href") && !$(this).attr("name")) {
				if ($(this).data('p')) {
					var parm = $(this).data('p');
				} else {
					var parm = $(this).parent().next().find("input,select,textarea").attr('name');
				}
				window.open(wikihref + parm);
			}
		});
	}

	// Title
	var pagename = (typeof page != 'undefined' ? page : $(location).attr('pathname').replace(/.*\/|\.[^.]*$/g, ''));
	$(document).attr('title', $(document).attr('title') + ' (' + pagename[0].toUpperCase() + pagename.slice(1) + ')');

	if (typeof page != 'undefined') {

		switch (page) {

		case 'livelog':

			if (!nostorage) {
				for (var i = 1; i < 6; i++) {
					var pattern = localStorage['regex' + i];
					var color = localStorage['color' + i];
					var fcolor = localStorage['fcolor' + i];
					$('#regex' + i).val(pattern ? pattern : '');
					$('#color' + i).val(color ? color : $('.colorPicker_def_color').css('color'));
					$('#fcolor' + i).val(fcolor ? fcolor : $('.colorPicker_def_fcolor').css('color'));
					$('#color' + i).colorPicker();
					$('#fcolor' + i).colorPicker();
					$('#whitelisted' + i).prop('checked', localStorage['whitelisted' + i] == '1' ? true : false);
					$('#hidden' + i).prop('checked', localStorage['hidden' + i] == '1' ? true : false);
				}
			}
			waitForMsg();

			break;

		case 'status':

			$(".status tbody:empty").hide();
			$("#chart").hide();
			if (!nostorage) {
				if (localStorage.loi == 'Login*') {
					$("#onlineidle")
						.text('Login*')
						.css('cursor','pointer')
						.attr('title', 'Online & Idle info (click to switch)');
				} else {
					$("#onlineidle")
						.text('Online & Idle*')
						.css('cursor','pointer')
						.attr('title', 'Login info (click to switch)');
				}
			}
			break;

		default:
			//do nothing

			break;
		}

		// if pollrefresh set to 0 hide pollselector
		setPollrefresh();
		if (pollrefresh) {
			$(":text[name='pintervall']").val(pollintervall / 1000);
			$("#poll").show();
			waitForMsg();
		}
	}
});

/**
 * Really Simple Color Picker in jQuery
 *
 * Licensed under the MIT (MIT-LICENSE.txt) licenses.
 *
 * Copyright (c) 2008-2012
 * Lakshan Perera (www.laktek.com) & Daniel Lacy (daniellacy.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
(function (e) {
	var t, n, r = 0,
		i = {
			control: e('<div class="colorPicker-picker">&nbsp;</div>'),
			palette: e('<div id="colorPicker_palette" class="colorPicker-palette" />'),
			swatch: e('<div class="colorPicker-swatch">&nbsp;</div>'),
			hexLabel: e('<label for="colorPicker_hex">Hex</label>'),
			hexField: e('<input type="text" id="colorPicker_hex" />')
		},
		s = "transparent",
		o;
	e.fn.colorPicker = function (t) {
		return this.each(function () {
			var n = e(this),
				o = e.extend({}, e.fn.colorPicker.defaults, t),
				u = e.fn.colorPicker.toHex(n.val().length > 0 ? n.val() : o.pickerDefault),
				a = i.control.clone(),
				f = i.palette.clone().attr("id", "colorPicker_palette-" + r),
				l = i.hexLabel.clone(),
				c = i.hexField.clone(),
				h = f[0].id,
				p, d;
			e.each(o.colors, function (t) {
				p = i.swatch.clone();
				if (o.colors[t] === s) {
					p.addClass(s).text("X");
					e.fn.colorPicker.bindPalette(c, p, s)
				} else {
					p.css("background-color", "#" + this);
					e.fn.colorPicker.bindPalette(c, p)
				}
				p.appendTo(f)
			});
			l.attr("for", "colorPicker_hex-" + r);
			c.attr({
				id: "colorPicker_hex-" + r,
				value: u
			});
			c.bind("keydown", function (t) {
				if (t.keyCode === 13) {
					var r = e.fn.colorPicker.toHex(e(this).val());
					e.fn.colorPicker.changeColor(r ? r : n.val())
				}
				if (t.keyCode === 27) {
					e.fn.colorPicker.hidePalette()
				}
			});
			c.bind("keyup", function (t) {
				var r = e.fn.colorPicker.toHex(e(t.target).val());
				e.fn.colorPicker.previewColor(r ? r : n.val())
			});
			c.bind("blur", function (t) {
				var r = e.fn.colorPicker.toHex(e(this).val());
				e.fn.colorPicker.changeColor(r ? r : n.val())
			});
			e('<div class="colorPicker_hexWrap" />').append(l).appendTo(f);
			f.find(".colorPicker_hexWrap").append(c);
			if (o.showHexField === false) {
				c.hide();
				l.hide()
			}
			e("body").append(f);
			f.hide();
			a.css("background-color", u);
			a.bind("click", function () {
				if (n.is(":not(:disabled)")) {
					e.fn.colorPicker.togglePalette(e("#" + h), e(this))
				}
			});
			if (t && t.onColorChange) {
				a.data("onColorChange", t.onColorChange)
			} else {
				a.data("onColorChange", function () {})
			} if (d = n.data("text")) a.html(d);
			n.after(a);
			n.bind("change", function () {
				n.next(".colorPicker-picker").css("background-color", e.fn.colorPicker.toHex(e(this).val()))
			});
			n.val(u);
			if (n[0].tagName.toLowerCase() === "input") {
				try {
					n.attr("type", "hidden")
				} catch (v) {
					n.css("visibility", "hidden").css("position", "absolute")
				}
			} else {
				n.hide()
			}
			r++
		})
	};
	e.extend(true, e.fn.colorPicker, {
		toHex: function (e) {
			if (e.match(/[0-9A-F]{6}|[0-9A-F]{3}$/i)) {
				return e.charAt(0) === "#" ? e : "#" + e
			} else if (e.match(/^rgb\(\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})\s*\)$/)) {
				var t = [parseInt(RegExp.$1, 10), parseInt(RegExp.$2, 10), parseInt(RegExp.$3, 10)],
					n = function (e) {
						if (e.length < 2) {
							for (var t = 0, n = 2 - e.length; t < n; t++) {
								e = "0" + e
							}
						}
						return e
					};
				if (t.length === 3) {
					var r = n(t[0].toString(16)),
						i = n(t[1].toString(16)),
						s = n(t[2].toString(16));
					return "#" + r + i + s
				}
			} else {
				return false
			}
		},
		checkMouse: function (r, i) {
			var s = n,
				o = e(r.target).parents("#" + s.attr("id")).length;
			if (r.target === e(s)[0] || r.target === t[0] || o > 0) {
				return
			}
			e.fn.colorPicker.hidePalette()
		},
		hidePalette: function () {
			e(document).unbind("mousedown", e.fn.colorPicker.checkMouse);
			e(".colorPicker-palette").hide()
		},
		showPalette: function (n) {
			var r = t.prev("input").val();
			n.css({
				top: t.offset().top + t.outerHeight(),
				left: t.offset().left
			});
			e("#color_value").val(r);
			n.show();
			e(document).bind("mousedown", e.fn.colorPicker.checkMouse)
		},
		togglePalette: function (r, i) {
			if (i) {
				t = i
			}
			n = r;
			if (n.is(":visible")) {
				e.fn.colorPicker.hidePalette()
			} else {
				e.fn.colorPicker.showPalette(r)
			}
		},
		changeColor: function (n) {
			t.css("background-color", n);
			t.prev("input").val(n).change();
			e.fn.colorPicker.hidePalette();
			t.data("onColorChange").call(t, e(t).prev("input").attr("id"), n)
		},
		previewColor: function (e) {
			t.css("background-color", e)
		},
		bindPalette: function (n, r, i) {
			i = i ? i : e.fn.colorPicker.toHex(r.css("background-color"));
			r.bind({
				click: function (t) {
					o = i;
					e.fn.colorPicker.changeColor(i)
				},
				mouseover: function (t) {
					o = n.val();
					e(this).css("border-color", "#598FEF");
					n.val(i);
					e.fn.colorPicker.previewColor(i)
				},
				mouseout: function (r) {
					e(this).css("border-color", "#000");
					n.val(t.css("background-color"));
					n.val(o);
					e.fn.colorPicker.previewColor(o)
				}
			})
		}
	});
	e.fn.colorPicker.defaults = {
		pickerDefault: "FFFFFF",
		colors: ["000000", "993300", "333300", "000080", "333399", "333333", "800000", "FF6600", "808000", "008000", "008080", "0000FF", "666699", "808080", "FF0000", "FF9900", "99CC00", "339966", "33CCCC", "3366FF", "800080", "999999", "FF00FF", "FFCC00", "FFFF00", "00FF00", "00FFFF", "00CCFF", "993366", "C0C0C0", "FF99CC", "FFCC99", "FFFF99", "CCFFFF", "99CCFF", "FFFFFF"],
		addColors: [],
		showHexField: true
	}
})(jQuery);
/**
 * Stupid jQuery Table Sort
 * Copyright (c) 2012 Joseph McCullough
 * https://github.com/joequery/Stupid-Table-Plugin#readme
 */
(function (e) {
	e.fn.stupidtable = function (t) {
		return this.each(function () {
			var n = e(this);
			t = t || {};
			t = e.extend({}, e.fn.stupidtable.default_sort_fns, t);
			n.on("click.stupidtable", "th", function () {
				var r = e(this);
				var i = 0;
				var s = e.fn.stupidtable.dir;
				n.find("#headline > th").slice(0, r.index()).each(function () {
					var t = e(this).attr("colspan") || 1;
					i += parseInt(t, 10)
				});
				var o = r.data("sort-default") || s.ASC;
				if (r.data("sort-dir")) o = r.data("sort-dir") === s.ASC ? s.DESC : s.ASC;
				var u = r.data("sort") || null;
				if (u === null) {
					return
				}
				n.trigger("beforetablesort", {
					column: i,
					direction: o
				});
				n.css("display");
				setTimeout(function () {
					var a = [];
					var f = t[u];
					var l = n.children("tbody").children("tr");
					l.each(function (t, n) {
						var r = e(n).children().eq(i);
						var s = r.data("sort-value");
						var o = typeof s !== "undefined" ? s : r.text();
						a.push([o, n])
					});
					a.sort(function (e, t, s) {
						return f(e[0], t[0], o)
					});
					if (o != s.ASC) a.reverse();
					l = e.map(a, function (e) {
						return e[1]
					});
					n.children("tbody").append(l);
					n.find("th.sorting-desc, th.sorting-asc").data("sort-dir", null).removeClass("sorting-desc sorting-asc").addClass("sortable");
					r.data("sort-dir", o).removeClass("sortable").addClass("sorting-" + o);
					n.trigger("aftertablesort", {
						column: i,
						direction: o
					});
					n.css("display")
				}, 10)
			})
		})
	};
	e.fn.stupidtable.dir = {
		ASC: "asc",
		DESC: "desc"
	};
	var convert_locale = function (c) {
		if (c == "") return 0;
		if(locale_decpoint == ",") {
			c = c.toString().replace( /\./g,"" ).replace( /,/,"." );
		}else if(locale_decpoint == "."){
			c = c.toString().replace( /,/g,"" );
		}
		return(c);
	}
	var ip2int = function dot2num(dot, s) {
		if (dot == ""  && s == "asc")  return 4300000000;
		if (dot == ""  && s == "desc") return 1;
		var d = dot.split('.');
			return ((((((+d[0])*256)+(+d[1]))*256)+(+d[2]))*256)+(+d[3]);
	}
	e.fn.stupidtable.default_sort_fns = {
		"int": function (e, t, s) {
			return parseInt(convert_locale(e), 10) - parseInt(convert_locale(t), 10)
		},
		"float": function (e, t, s) {
			return parseFloat(convert_locale(e)) - parseFloat(convert_locale(t))
		},
		"ip": function (a, b, s) {
			aIP = ip2int(a, s);
			bIP = ip2int(b, s);
			return aIP - bIP;
		},
		"string": function (e, t, s) {
			if (e == "" && s == "asc") return +1;
			if (t == "" && s == "asc") return -1;
			if (e < t) return -1;
			if (e > t) return +1;
			return 0
		},
		"string-ins": function (e, t, s) {
			e = e.toString().toLowerCase();
			t = t.toString().toLowerCase();
			if (e == "" && s == "asc") return +1;
			if (t == "" && s == "asc") return -1;
			if (e < t) return -1;
			if (e > t) return +1;
			return 0
		}
	}
})(jQuery)
